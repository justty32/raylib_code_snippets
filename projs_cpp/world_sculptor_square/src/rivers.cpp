// 河流系統實作。分兩層：
//   1) 存儲層 — 把河流流量打包進 Tile.rivers 的 bit 欄位 (見 rivers.hpp 的擁有權說明)。
//   2) 生成層 — 由高度圖 + 降雨量推導河網，整體流程：
//        海岸水格找出海口 (seeds)
//          → 反向 Dijkstra 從出海口往內陸建「下游樹」(_flood_paths)
//          → 沿樹後序累積流量、扣蒸發 (_accumulate_flow)
//          → 從流量足夠的出海口往上游描繪主流與支流 (_create_rivers_from_seed)
//      設計參考 RimWorld 的河流生成，改為方格 4 鄰居版。
#include "mapcore/rivers.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace mapcore {

// 四個角依序：NE=0, NW=1, SW=2, SE=3。
// dir 0=E 邊連 NE-SE = (0, 3)
// dir 1=N 邊連 NE-NW = (0, 1)
// dir 2=W 邊連 NW-SW = (1, 2)
// dir 3=S 邊連 SW-SE = (2, 3)
const std::array<std::pair<int,int>, 4> EDGE_CORNERS = {{
    {0, 3},  // d=0 E
    {0, 1},  // d=1 N
    {1, 2},  // d=2 W
    {2, 3},  // d=3 S
}};

constexpr float LOG_STRENGTH_SCALE   = 45.0f;
constexpr int   CREEK_THRESHOLD      = 80;
constexpr int   LARGE_RIVER_THRESHOLD= 160;

RiverClass classify_river_strength(int strength) noexcept {
    if (strength < CREEK_THRESHOLD)       return RiverClass::CREEK;
    if (strength < LARGE_RIVER_THRESHOLD) return RiverClass::RIVER;
    return RiverClass::LARGE_RIVER;
}

// ── 存儲層 ─────────────────────────────────────────────────────────────────

// 回傳 (owner.x, owner.y, slot)；direction < 2 自己擁有，其餘鄰居擁有
static std::tuple<int,int,int> edge_owner(const Coord& c, int direction) noexcept {
    if (direction < 2)
        return {c.x, c.y, direction};
    Coord nb = c + DIRECTIONS[direction];
    return {nb.x, nb.y, direction - 2};
}

static int read_slot(uint32_t rivers, int slot) noexcept {
    return static_cast<int>((rivers >> (slot * RIVER_BITS)) & RIVER_MASK);
}

static uint32_t write_slot(uint32_t rivers, int slot, int value) noexcept {
    value = std::max(0, std::min(RIVER_MAX_STRENGTH, value));
    uint32_t mask = static_cast<uint32_t>(RIVER_MASK) << (slot * RIVER_BITS);
    return (rivers & ~mask) | (static_cast<uint32_t>(value) << (slot * RIVER_BITS));
}

int get_river_strength(const TileMap& map, const Coord& c, int direction) noexcept {
    auto [ox, oy, slot] = edge_owner(c, direction);
    const Tile* t = map.get({ox, oy});
    if (!t) return 0;
    return read_slot(t->rivers, slot);
}

void set_river_strength(TileMap& map, const Coord& c, int direction, int strength) noexcept {
    auto [ox, oy, slot] = edge_owner(c, direction);
    Tile* t = map.get({ox, oy});
    if (!t) return;
    t->rivers = write_slot(t->rivers, slot, strength);
}

void add_river_flow(TileMap& map, const Coord& c, int direction, int amount) noexcept {
    auto [ox, oy, slot] = edge_owner(c, direction);
    Tile* t = map.get({ox, oy});
    if (!t) return;
    int cur = read_slot(t->rivers, slot);
    t->rivers = write_slot(t->rivers, slot, cur + amount);
}

bool has_river_edge(const TileMap& map, const Coord& c, int direction) noexcept {
    return get_river_strength(map, c, direction) > 0;
}

void set_river_edge(TileMap& map, const Coord& c, int direction, bool value) noexcept {
    set_river_strength(map, c, direction, value ? 1 : 0);
}

void iter_river_edges(const TileMap& map, std::function<void(const Coord&, int, int)> cb) {
    map.for_each([&](const Coord& c, const Tile& tile) {
        if (!tile.rivers) return;
        for (int slot = 0; slot < 2; ++slot) {
            int s = read_slot(tile.rivers, slot);
            if (s > 0) cb(c, slot, s);
        }
    });
}

// ── 生成：內部工具函式 ──────────────────────────────────────────────────────

static bool _is_water(const TileMap& map, const Coord& c) {
    const Tile* t = map.get(c);
    if (!t) return false;
    return get_default_registry().is_water(t->terrain);
}

static int _manhattan(const Coord& a, const Coord& b) noexcept {
    return grid_distance(a, b);
}

static float _elevation_change_cost(float delta) noexcept {
    if (delta < -1.0f) return 50.0f;
    if (delta < -0.1f) { float t = (delta + 1.0f) / 0.9f; return 50.0f + t * 50.0f; }
    if (delta <  0.0f) { float t = (delta + 0.1f) / 0.1f; return 100.0f + t * 300.0f; }
    if (delta <  0.1f) { float t = delta / 0.1f;           return 5000.0f + t * 45000.0f; }
    return 50000.0f;
}

static float _approximate_temperature(int y, int total_h) noexcept {
    float half = std::max((total_h - 1) / 2.0f, 1e-9f);
    float lat_abs = std::abs(y - (total_h - 1) / 2.0f) / half;
    return 25.0f - 55.0f * lat_abs;
}

static float _evaporation_constant(float temp_c) noexcept {
    return 0.61121f * std::exp((18.678f - temp_c / 234.5f) * (temp_c / (257.14f + temp_c))) / (temp_c + 273.0f);
}

static float _total_evaporation(float flow, float temp_c, float scale) noexcept {
    if (flow <= 0.0f) return 0.0f;
    return _evaporation_constant(temp_c) * std::sqrt(flow) * 250.0f * scale;
}

static std::vector<Coord> _get_coastal_water_tiles(const TileMap& map) {
    std::vector<Coord> result;
    map.for_each([&](const Coord& c, const Tile& tile) {
        if (!get_default_registry().is_water(tile.terrain)) return;
        for (const auto& d : DIRECTIONS) {
            Coord nb = c + d;
            const Tile* nt = map.get(nb);
            if (nt && !get_default_registry().is_water(nt->terrain)) {
                result.push_back(c);
                return;
            }
        }
    });
    return result;
}

static std::unordered_map<int, int> _compute_water_component_sizes(const TileMap& map) {
    const int W = map.width();
    const int H = map.height();
    std::vector<bool> visited(W * H, false);
    std::unordered_map<int, int> result;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = y * W + x;
            if (visited[idx]) continue;
            if (!get_default_registry().is_water(map.tile_at(x, y).terrain)) {
                visited[idx] = true;
                continue;
            }
            std::vector<int> comp;
            std::vector<int> stk = {idx};
            visited[idx] = true;
            while (!stk.empty()) {
                int cur = stk.back(); stk.pop_back();
                comp.push_back(cur);
                int cx = cur % W, cy = cur / W;
                for (const auto& d : DIRECTIONS) {
                    Coord nb{cx + d.x, cy + d.y};
                    if (!map.in_bounds(nb)) continue;
                    int ni = nb.y * W + nb.x;
                    if (visited[ni]) continue;
                    if (!get_default_registry().is_water(map.tile_at(nb.x, nb.y).terrain)) continue;
                    visited[ni] = true;
                    stk.push_back(ni);
                }
            }
            int sz = static_cast<int>(comp.size());
            for (int ci : comp) result[ci] = sz;
        }
    }
    return result;
}

static std::vector<Coord> _downsample_seeds(
    const std::vector<Coord>& seeds, const TileMap& map,
    const std::vector<float>& heightmap, int min_dist)
{
    if (min_dist <= 1 || seeds.empty()) return seeds;
    const int W = map.width();
    auto min_adj_elev = [&](const Coord& c) {
        float mn = 1.0f;
        for (const auto& d : DIRECTIONS) {
            Coord nb = c + d;
            if (!map.in_bounds(nb)) continue;
            if (get_default_registry().is_water(map.tile_at(nb.x, nb.y).terrain)) continue;
            mn = std::min(mn, heightmap[nb.y * W + nb.x]);
        }
        return mn;
    };
    std::vector<Coord> ordered = seeds;
    std::stable_sort(ordered.begin(), ordered.end(), [&](const Coord& a, const Coord& b) {
        return min_adj_elev(a) < min_adj_elev(b);
    });
    std::vector<Coord> kept;
    for (const auto& s : ordered) {
        bool ok = true;
        for (const auto& k : kept) {
            if (_manhattan(s, k) < min_dist) { ok = false; break; }
        }
        if (ok) kept.push_back(s);
    }
    return kept;
}

// 反向 Dijkstra 建下游樹
static std::vector<int> _flood_paths(
    const TileMap& map,
    const std::vector<float>& heightmap,
    const std::vector<Coord>& seeds)
{
    const int W = map.width();
    const int H = map.height();
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> g(W * H, INF);
    std::vector<int>   parent(W * H, -1);

    std::unordered_set<int> seed_set;
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;
    for (const auto& s : seeds) {
        int idx = s.y * W + s.x;
        if (g[idx] > 0.0f) { g[idx] = 0.0f; pq.push({0.0f, idx}); seed_set.insert(idx); }
    }
    while (!pq.empty()) {
        auto [gv, cidx] = pq.top(); pq.pop();
        if (gv > g[cidx]) continue;
        int cx = cidx % W, cy = cidx / W;
        Coord cur{cx, cy};
        if (seed_set.find(cidx) == seed_set.end() && _is_water(map, cur)) continue;
        float cur_elev = heightmap[cidx];
        for (const auto& d : DIRECTIONS) {
            Coord nb = cur + d;
            if (!map.in_bounds(nb)) continue;
            int nidx = nb.y * W + nb.x;
            float nb_elev = heightmap[nidx];
            // 找 nb 的最低鄰居（factor=1 if cur is lowest neighbor of nb）
            float lowest = INF; int lowest_idx = -1;
            for (const auto& d2 : DIRECTIONS) {
                Coord nbnb = nb + d2;
                if (!map.in_bounds(nbnb)) continue;
                int ni2 = nbnb.y * W + nbnb.x;
                if (heightmap[ni2] < lowest) { lowest = heightmap[ni2]; lowest_idx = ni2; }
            }
            float factor = (lowest_idx == cidx) ? 1.0f : 2.0f;
            float delta = cur_elev - nb_elev;
            float new_g = gv + factor * _elevation_change_cost(delta);
            if (new_g < g[nidx]) {
                g[nidx] = new_g;
                parent[nidx] = cidx;
                pq.push({new_g, nidx});
            }
        }
    }
    return parent;
}

static std::vector<std::vector<int>> _build_children(int N, const std::vector<int>& parent) {
    std::vector<std::vector<int>> children(N);
    for (int i = 0; i < N; ++i) {
        if (parent[i] >= 0) children[parent[i]].push_back(i);
    }
    return children;
}

static void _accumulate_flow(
    std::vector<float>& flow,
    const std::vector<std::vector<int>>& children,
    int root,
    const std::vector<float>& rainfall,
    const std::vector<float>& temperature,
    float evap_scale)
{
    std::vector<std::pair<int,bool>> stk;
    stk.push_back({root, false});
    while (!stk.empty()) {
        auto [cur, processed] = stk.back(); stk.pop_back();
        if (!processed) {
            stk.push_back({cur, true});
            for (int ch : children[cur]) stk.push_back({ch, false});
            continue;
        }
        flow[cur] += rainfall[cur];
        for (int ch : children[cur]) flow[cur] += flow[ch];
        float evap = _total_evaporation(flow[cur], temperature[cur], evap_scale);
        flow[cur] = std::max(0.0f, flow[cur] - evap);
    }
}

static void _paint_edge(TileMap& map, const Coord& cur, int direction, float flow_value, float scale) {
    float ref = flow_value * scale;
    int strength = std::max(1, std::min(RIVER_MAX_STRENGTH,
        static_cast<int>(std::log1p(ref) * LOG_STRENGTH_SCALE)));
    add_river_flow(map, cur, direction, strength);
}

static int _create_rivers_from_seed(
    TileMap& map, const std::vector<float>& flow,
    const std::vector<std::vector<int>>& children,
    int seed_idx, int W,
    float spawn_flow_threshold,
    float degrade_threshold,
    float branch_flow_threshold,
    float branch_chance,
    float flow_strength_scale,
    std::mt19937& rng)
{
    Coord seed{seed_idx % W, seed_idx / W};
    int painted = 0;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int first_child_idx : children[seed_idx]) {
        float cf = flow[first_child_idx];
        if (cf < spawn_flow_threshold) continue;
        Coord first_child{first_child_idx % W, first_child_idx / W};
        int fd = -1;
        for (int d = 0; d < 4; ++d) {
            if (seed + DIRECTIONS[d] == first_child) { fd = d; break; }
        }
        if (fd < 0) continue;
        _paint_edge(map, seed, fd, cf, flow_strength_scale);
        ++painted;

        std::vector<int> stk = {first_child_idx};
        while (!stk.empty()) {
            int cur_idx = stk.back(); stk.pop_back();
            const auto& kids = children[cur_idx];
            if (kids.empty()) continue;
            Coord cur{cur_idx % W, cur_idx / W};
            std::vector<int> sorted_kids = kids;
            std::stable_sort(sorted_kids.begin(), sorted_kids.end(), [&](int a, int b) {
                return flow[a] > flow[b];
            });
            int best_idx = sorted_kids[0];
            float best_flow = flow[best_idx];
            if (best_flow >= degrade_threshold) {
                Coord best{best_idx % W, best_idx / W};
                int bd = -1;
                for (int d = 0; d < 4; ++d) {
                    if (cur + DIRECTIONS[d] == best) { bd = d; break; }
                }
                if (bd >= 0) { _paint_edge(map, cur, bd, best_flow, flow_strength_scale); ++painted; }
                stk.push_back(best_idx);
            }
            for (size_t i = 1; i < sorted_kids.size(); ++i) {
                int alt_idx = sorted_kids[i];
                float af = flow[alt_idx];
                if (af < branch_flow_threshold) continue;
                if (dist(rng) >= branch_chance) continue;
                Coord alt{alt_idx % W, alt_idx / W};
                int ad = -1;
                for (int d = 0; d < 4; ++d) {
                    if (cur + DIRECTIONS[d] == alt) { ad = d; break; }
                }
                if (ad >= 0) { _paint_edge(map, cur, ad, af, flow_strength_scale); ++painted; }
                stk.push_back(alt_idx);
            }
        }
    }
    return painted;
}

// ── 公開生成入口 ─────────────────────────────────────────────────────────────

int generate_rivers(
    TileMap& tile_map,
    const std::vector<float>& heightmap,
    const std::vector<float>& rainfall,
    const std::vector<float>* temperature,
    std::optional<uint64_t> seed,
    const RiverGenParams& p)
{
    const int W = tile_map.width();
    const int H = tile_map.height();
    const int N = W * H;

    std::mt19937 rng(seed.has_value() ? static_cast<uint32_t>(*seed) : std::random_device{}());

    std::vector<Coord> seeds = _get_coastal_water_tiles(tile_map);
    if (seeds.empty()) return 0;

    if (p.min_sea_size > 1) {
        auto comp = _compute_water_component_sizes(tile_map);
        std::vector<Coord> filtered;
        for (const auto& s : seeds) {
            int idx = s.y * W + s.x;
            auto it = comp.find(idx);
            if (it != comp.end() && it->second >= p.min_sea_size) filtered.push_back(s);
        }
        seeds = std::move(filtered);
        if (seeds.empty()) return 0;
    }
    if (p.min_seed_spacing > 1)
        seeds = _downsample_seeds(seeds, tile_map, heightmap, p.min_seed_spacing);
    if (seeds.empty()) return 0;

    auto parent   = _flood_paths(tile_map, heightmap, seeds);
    auto children = _build_children(N, parent);

    std::vector<float> temp_grid(N);
    if (temperature) {
        temp_grid = *temperature;
    } else {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                temp_grid[y * W + x] = _approximate_temperature(y, H);
    }

    std::vector<float> rain_scaled(N);
    for (int i = 0; i < N; ++i) rain_scaled[i] = rainfall[i] * p.rainfall_scale;

    std::vector<float> flow(N, 0.0f);
    for (const auto& s : seeds)
        _accumulate_flow(flow, children, s.y * W + s.x, rain_scaled, temp_grid, p.evaporation_scale);

    int painted = 0;
    for (const auto& s : seeds)
        painted += _create_rivers_from_seed(
            tile_map, flow, children, s.y * W + s.x, W,
            p.spawn_flow_threshold, p.degrade_threshold,
            p.branch_flow_threshold, p.branch_chance,
            p.flow_strength_scale, rng);
    return painted;
}

} // namespace mapcore

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

const std::array<std::pair<int,int>, 6> EDGE_CORNERS = {{
    {0, 1},  // d=0 E
    {5, 0},  // d=1 NE
    {4, 5},  // d=2 NW
    {3, 4},  // d=3 W
    {2, 3},  // d=4 SW
    {1, 2},  // d=5 SE
}};

// ── log scale 常數（對齊 Python） ───────────────────────────────────────────
constexpr float LOG_STRENGTH_SCALE   = 45.0f;
constexpr int   CREEK_THRESHOLD      = 80;
constexpr int   LARGE_RIVER_THRESHOLD= 160;

RiverClass classify_river_strength(int strength) noexcept {
    if (strength < CREEK_THRESHOLD)       return RiverClass::CREEK;
    if (strength < LARGE_RIVER_THRESHOLD) return RiverClass::RIVER;
    return RiverClass::LARGE_RIVER;
}

// ── 存儲層 ─────────────────────────────────────────────────────────────────

// 回傳 (owner hex q, owner hex r, slot index)；direction < 3 = 自己擁有
static std::tuple<int,int,int> edge_owner(const Hex& h, int direction) noexcept {
    if (direction < 3)
        return {h.q, h.r, direction};
    Hex nb = h + DIRECTIONS[direction];
    return {nb.q, nb.r, direction - 3};
}

static int read_slot(uint32_t rivers, int slot) noexcept {
    return static_cast<int>((rivers >> (slot * RIVER_BITS)) & RIVER_MASK);
}

static uint32_t write_slot(uint32_t rivers, int slot, int value) noexcept {
    value = std::max(0, std::min(RIVER_MAX_STRENGTH, value));
    uint32_t mask = static_cast<uint32_t>(RIVER_MASK) << (slot * RIVER_BITS);
    return (rivers & ~mask) | (static_cast<uint32_t>(value) << (slot * RIVER_BITS));
}

int get_river_strength(const TileMap& map, const Hex& h, int direction) noexcept {
    auto [oq, or_, slot] = edge_owner(h, direction);
    const Tile* t = map.get({oq, or_});
    if (!t) return 0;
    return read_slot(t->rivers, slot);
}

void set_river_strength(TileMap& map, const Hex& h, int direction, int strength) noexcept {
    auto [oq, or_, slot] = edge_owner(h, direction);
    Tile* t = map.get({oq, or_});
    if (!t) return;
    t->rivers = write_slot(t->rivers, slot, strength);
}

void add_river_flow(TileMap& map, const Hex& h, int direction, int amount) noexcept {
    auto [oq, or_, slot] = edge_owner(h, direction);
    Tile* t = map.get({oq, or_});
    if (!t) return;
    int cur = read_slot(t->rivers, slot);
    t->rivers = write_slot(t->rivers, slot, cur + amount);
}

bool has_river_edge(const TileMap& map, const Hex& h, int direction) noexcept {
    return get_river_strength(map, h, direction) > 0;
}

void set_river_edge(TileMap& map, const Hex& h, int direction, bool value) noexcept {
    set_river_strength(map, h, direction, value ? 1 : 0);
}

void iter_river_edges(const TileMap& map, std::function<void(const Hex&, int, int)> cb) {
    map.for_each([&](const Hex& h, const Tile& tile) {
        if (!tile.rivers) return;
        for (int slot = 0; slot < 3; ++slot) {
            int s = read_slot(tile.rivers, slot);
            if (s > 0) cb(h, slot, s);
        }
    });
}

// ── 生成：內部工具函式 ──────────────────────────────────────────────────────

static bool _is_water(const TileMap& map, const Hex& h) {
    const Tile* t = map.get(h);
    if (!t) return false;
    return get_default_registry().is_water(t->terrain);
}

static int _hex_dist(const Hex& a, const Hex& b) noexcept {
    return hex_distance(a, b);
}

static float _elevation_change_cost(float delta) noexcept {
    // 對齊 Python _elevation_change_cost，delta = elev(下游) - elev(上游)
    if (delta < -1.0f) return 50.0f;
    if (delta < -0.1f) { float t = (delta + 1.0f) / 0.9f; return 50.0f + t * 50.0f; }
    if (delta <  0.0f) { float t = (delta + 0.1f) / 0.1f; return 100.0f + t * 300.0f; }
    if (delta <  0.1f) { float t = delta / 0.1f;           return 5000.0f + t * 45000.0f; }
    return 50000.0f;
}

static float _approximate_temperature(int r, int total_h) noexcept {
    float half = std::max((total_h - 1) / 2.0f, 1e-9f);
    float lat_abs = std::abs(r - (total_h - 1) / 2.0f) / half;
    return 25.0f - 55.0f * lat_abs;
}

static float _evaporation_constant(float temp_c) noexcept {
    return 0.61121f * std::exp((18.678f - temp_c / 234.5f) * (temp_c / (257.14f + temp_c))) / (temp_c + 273.0f);
}

static float _total_evaporation(float flow, float temp_c, float scale) noexcept {
    if (flow <= 0.0f) return 0.0f;
    return _evaporation_constant(temp_c) * std::sqrt(flow) * 250.0f * scale;
}

static std::vector<Hex> _get_coastal_water_tiles(const TileMap& map) {
    std::vector<Hex> result;
    map.for_each([&](const Hex& h, const Tile& tile) {
        if (!get_default_registry().is_water(tile.terrain)) return;
        for (const auto& d : DIRECTIONS) {
            Hex nb = h + d;
            const Tile* nt = map.get(nb);
            if (nt && !get_default_registry().is_water(nt->terrain)) {
                result.push_back(h);
                return;
            }
        }
    });
    return result;
}

// BFS: 回傳每個水格所屬連通分量大小的 flat 陣列索引（(q,r) → size）
static std::unordered_map<int, int> _compute_water_component_sizes(const TileMap& map) {
    const int W = map.width();
    const int H = map.height();
    std::vector<bool> visited(W * H, false);
    std::unordered_map<int, int> result;
    for (int r = 0; r < H; ++r) {
        for (int q = 0; q < W; ++q) {
            int idx = r * W + q;
            if (visited[idx]) continue;
            if (!get_default_registry().is_water(map.tile_at(q, r).terrain)) {
                visited[idx] = true;
                continue;
            }
            std::vector<int> comp;
            std::vector<int> stk = {idx};
            visited[idx] = true;
            while (!stk.empty()) {
                int cur = stk.back(); stk.pop_back();
                comp.push_back(cur);
                int cq = cur % W, cr = cur / W;
                for (const auto& d : DIRECTIONS) {
                    Hex nb{cq + d.q, cr + d.r};
                    if (!map.in_bounds(nb)) continue;
                    int ni = nb.r * W + nb.q;
                    if (visited[ni]) continue;
                    if (!get_default_registry().is_water(map.tile_at(nb.q, nb.r).terrain)) continue;
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

// 貪心 seed 間距過濾：保留彼此距離 >= min_dist 的 seed（低窪優先）
static std::vector<Hex> _downsample_seeds(
    const std::vector<Hex>& seeds, const TileMap& map,
    const std::vector<float>& heightmap, int min_dist)
{
    if (min_dist <= 1 || seeds.empty()) return seeds;
    const int W = map.width();
    auto min_adj_elev = [&](const Hex& h) {
        float mn = 1.0f;
        for (const auto& d : DIRECTIONS) {
            Hex nb = h + d;
            if (!map.in_bounds(nb)) continue;
            if (get_default_registry().is_water(map.tile_at(nb.q, nb.r).terrain)) continue;
            mn = std::min(mn, heightmap[nb.r * W + nb.q]);
        }
        return mn;
    };
    std::vector<Hex> ordered = seeds;
    std::stable_sort(ordered.begin(), ordered.end(), [&](const Hex& a, const Hex& b) {
        return min_adj_elev(a) < min_adj_elev(b);
    });
    std::vector<Hex> kept;
    for (const auto& s : ordered) {
        bool ok = true;
        for (const auto& k : kept) {
            if (_hex_dist(s, k) < min_dist) { ok = false; break; }
        }
        if (ok) kept.push_back(s);
    }
    return kept;
}

// 反向 Dijkstra 建下游樹（對齊 _flood_paths_with_cost_for_tree）
// 回傳 parent flat 陣列：parent[r*W+q] = 下游格 index，-1 = seed/無 parent
static std::vector<int> _flood_paths(
    const TileMap& map,
    const std::vector<float>& heightmap,
    const std::vector<Hex>& seeds)
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
        int idx = s.r * W + s.q;
        if (g[idx] > 0.0f) { g[idx] = 0.0f; pq.push({0.0f, idx}); seed_set.insert(idx); }
    }
    while (!pq.empty()) {
        auto [gv, cidx] = pq.top(); pq.pop();
        if (gv > g[cidx]) continue;
        int cq = cidx % W, cr = cidx / W;
        Hex cur{cq, cr};
        // 非 seed 水格不繼續擴張
        if (seed_set.find(cidx) == seed_set.end() && _is_water(map, cur)) continue;
        float cur_elev = heightmap[cidx];
        for (const auto& d : DIRECTIONS) {
            Hex nb = cur + d;
            if (!map.in_bounds(nb)) continue;
            int nidx = nb.r * W + nb.q;
            float nb_elev = heightmap[nidx];
            // 找 nb 的最低鄰居（factor=1 if cur is lowest neighbor of nb）
            float lowest = INF; int lowest_idx = -1;
            for (const auto& d2 : DIRECTIONS) {
                Hex nbnb = nb + d2;
                if (!map.in_bounds(nbnb)) continue;
                int ni2 = nbnb.r * W + nbnb.q;
                if (heightmap[ni2] < lowest) { lowest = heightmap[ni2]; lowest_idx = ni2; }
            }
            float factor = (lowest_idx == cidx) ? 1.0f : 2.0f;
            float delta = cur_elev - nb_elev;  // cur 是下游
            float new_g = gv + factor * _elevation_change_cost(delta);
            if (new_g < g[nidx]) {
                g[nidx] = new_g;
                parent[nidx] = cidx;  // nb 的水流向 cur
                pq.push({new_g, nidx});
            }
        }
    }
    return parent;
}

// parent → children list（per tile）
static std::vector<std::vector<int>> _build_children(int N, const std::vector<int>& parent) {
    std::vector<std::vector<int>> children(N);
    for (int i = 0; i < N; ++i) {
        if (parent[i] >= 0) children[parent[i]].push_back(i);
    }
    return children;
}

// Post-order DFS 累加 flow
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

static void _paint_edge(TileMap& map, const Hex& cur, int direction, float flow_value, float scale) {
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
    Hex seed{seed_idx % W, seed_idx / W};
    int painted = 0;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int first_child_idx : children[seed_idx]) {
        float cf = flow[first_child_idx];
        if (cf < spawn_flow_threshold) continue;
        Hex first_child{first_child_idx % W, first_child_idx / W};
        // 找方向
        int fd = -1;
        for (int d = 0; d < 6; ++d) {
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
            Hex cur{cur_idx % W, cur_idx / W};
            // 排序 kids by flow descending
            std::vector<int> sorted_kids = kids;
            std::stable_sort(sorted_kids.begin(), sorted_kids.end(), [&](int a, int b) {
                return flow[a] > flow[b];
            });
            int best_idx = sorted_kids[0];
            float best_flow = flow[best_idx];
            if (best_flow >= degrade_threshold) {
                Hex best{best_idx % W, best_idx / W};
                int bd = -1;
                for (int d = 0; d < 6; ++d) {
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
                Hex alt{alt_idx % W, alt_idx / W};
                int ad = -1;
                for (int d = 0; d < 6; ++d) {
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

    std::vector<Hex> seeds = _get_coastal_water_tiles(tile_map);
    if (seeds.empty()) return 0;

    if (p.min_sea_size > 1) {
        auto comp = _compute_water_component_sizes(tile_map);
        std::vector<Hex> filtered;
        for (const auto& s : seeds) {
            int idx = s.r * W + s.q;
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

    // 溫度 grid（flat）
    std::vector<float> temp_grid(N);
    if (temperature) {
        temp_grid = *temperature;
    } else {
        for (int r = 0; r < H; ++r)
            for (int q = 0; q < W; ++q)
                temp_grid[r * W + q] = _approximate_temperature(r, H);
    }

    // rainfall scaled
    std::vector<float> rain_scaled(N);
    for (int i = 0; i < N; ++i) rain_scaled[i] = rainfall[i] * p.rainfall_scale;

    std::vector<float> flow(N, 0.0f);
    for (const auto& s : seeds)
        _accumulate_flow(flow, children, s.r * W + s.q, rain_scaled, temp_grid, p.evaporation_scale);

    int painted = 0;
    for (const auto& s : seeds)
        painted += _create_rivers_from_seed(
            tile_map, flow, children, s.r * W + s.q, W,
            p.spawn_flow_threshold, p.degrade_threshold,
            p.branch_flow_threshold, p.branch_chance,
            p.flow_strength_scale, rng);
    return painted;
}

} // namespace mapcore

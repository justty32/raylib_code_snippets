#include "mapcore/features.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace mapcore {

// ── WorldFeatures ─────────────────────────────────────────────────────────

WorldFeature& WorldFeatures::add(
    const std::string& feature_type,
    const std::string& name,
    std::vector<Hex> tiles,
    const Hex& center)
{
    WorldFeature f;
    f.id           = static_cast<int>(features.size());
    f.feature_type = feature_type;
    f.name         = name;
    f.size         = static_cast<int>(tiles.size());
    f.center       = center;
    f.tiles        = std::move(tiles);
    features.push_back(std::move(f));
    return features.back();
}

const WorldFeature* WorldFeatures::get(int feature_id) const noexcept {
    if (feature_id < 0 || feature_id >= static_cast<int>(features.size())) return nullptr;
    return &features[feature_id];
}

// ── 內部工具 ──────────────────────────────────────────────────────────────

// 找所有符合 predicate 的連通分量 (BFS)，回傳每個分量的 tiles list
static std::vector<std::vector<Hex>> find_components(
    const TileMap& map,
    std::function<bool(uint16_t)> predicate)
{
    const int W = map.width();
    const int H = map.height();
    std::vector<bool> visited(W * H, false);
    std::vector<std::vector<Hex>> result;
    for (int r = 0; r < H; ++r) {
        for (int q = 0; q < W; ++q) {
            int idx = r * W + q;
            if (visited[idx]) continue;
            if (!predicate(map.tile_at(q, r).terrain)) { visited[idx] = true; continue; }
            std::vector<Hex> comp;
            std::vector<Hex> stk = {{q, r}};
            visited[idx] = true;
            while (!stk.empty()) {
                Hex cur = stk.back(); stk.pop_back();
                comp.push_back(cur);
                for (const auto& d : DIRECTIONS) {
                    Hex nb = cur + d;
                    if (!map.in_bounds(nb)) continue;
                    int ni = nb.r * W + nb.q;
                    if (visited[ni]) continue;
                    visited[ni] = true;
                    if (predicate(map.tile_at(nb.q, nb.r).terrain)) stk.push_back(nb);
                }
            }
            result.push_back(std::move(comp));
        }
    }
    return result;
}

// 計算 tiles 的 axial 重心（找最近的成員，確保標籤落在 feature 內）
static Hex centroid(const std::vector<Hex>& tiles) {
    if (tiles.empty()) return {};
    float q_avg = 0, r_avg = 0;
    for (const auto& h : tiles) { q_avg += h.q; r_avg += h.r; }
    q_avg /= tiles.size(); r_avg /= tiles.size();
    int bq = tiles[0].q, br = tiles[0].r;
    float best = 1e18f;
    for (const auto& h : tiles) {
        float d = std::abs(h.q - q_avg) + std::abs(h.r - r_avg);
        if (d < best) { best = d; bq = h.q; br = h.r; }
    }
    return {bq, br};
}

// 給 tiles 上的每個 Tile 設 feature_id
static void assign_feature_id(TileMap& map, const std::vector<Hex>& tiles, int fid) {
    for (const auto& h : tiles) {
        Tile* t = map.get(h);
        if (t) t->feature_id = fid;
    }
}

// ── 內建 Worker 實作 ──────────────────────────────────────────────────────

// 共用 flood-fill worker 建構器
struct FloodFillWorker : FeatureWorker {
    std::string type_prefix;
    std::function<bool(uint16_t)> predicate;
    int min_size;
    FloodFillWorker(std::string tp, std::function<bool(uint16_t)> pred, int ms)
        : type_prefix(std::move(tp)), predicate(std::move(pred)), min_size(ms) {}
    void apply(TileMap& map, WorldFeatures& features) override {
        int counter = 0;
        for (auto& comp : find_components(map, predicate)) {
            if (static_cast<int>(comp.size()) < min_size) continue;
            Hex ctr = centroid(comp);
            std::string name = type_prefix + " #" + std::to_string(++counter);
            auto& f = features.add(type_prefix, name, comp, ctr);
            assign_feature_id(map, f.tiles, f.id);
        }
    }
};

std::unique_ptr<FeatureWorker> make_ocean_worker() {
    return std::make_unique<FloodFillWorker>(
        "Ocean",
        [](uint16_t t) { return t == TerrainType::OCEAN; },
        1);
}

std::unique_ptr<FeatureWorker> make_lake_worker() {
    return std::make_unique<FloodFillWorker>(
        "Lake",
        [](uint16_t t) { return t == TerrainType::LAKE || t == TerrainType::COAST; },
        1);
}

std::unique_ptr<FeatureWorker> make_coast_worker() {
    return std::make_unique<FloodFillWorker>(
        "Coast",
        [](uint16_t t) { return t == TerrainType::COAST; },
        3);
}

std::unique_ptr<FeatureWorker> make_mountain_range_worker(int min_size) {
    return std::make_unique<FloodFillWorker>(
        "MountainRange",
        [](uint16_t t) { return t == TerrainType::MOUNTAIN; },
        min_size);
}

std::unique_ptr<FeatureWorker> make_biome_region_worker(uint16_t terrain_id, int min_size) {
    std::string type_name = "BiomeRegion:" + get_default_registry().get(terrain_id).name;
    return std::make_unique<FloodFillWorker>(
        type_name,
        [terrain_id](uint16_t t) { return t == terrain_id; },
        min_size);
}

std::unique_ptr<FeatureWorker> make_island_worker(int min_size) {
    struct IslandWorker : FeatureWorker {
        int min_size_;
        explicit IslandWorker(int ms) : min_size_(ms) {}
        void apply(TileMap& map, WorldFeatures& features) override {
            const int W = map.width(), H = map.height();
            auto is_land = [&](uint16_t t) {
                return !get_default_registry().is_water(t);
            };
            int counter = 0;
            for (auto& comp : find_components(map, is_land)) {
                if (static_cast<int>(comp.size()) < min_size_) continue;
                // 不觸及地圖邊界
                bool on_edge = false;
                for (const auto& h : comp) {
                    if (h.q == 0 || h.q == W-1 || h.r == 0 || h.r == H-1) { on_edge = true; break; }
                }
                if (on_edge) continue;
                Hex ctr = centroid(comp);
                std::string name = "Island #" + std::to_string(++counter);
                auto& f = features.add("Island", name, comp, ctr);
                assign_feature_id(map, f.tiles, f.id);
            }
        }
    };
    return std::make_unique<IslandWorker>(min_size);
}

std::unique_ptr<FeatureWorker> make_continent_worker(int top_n, int min_size) {
    struct ContinentWorker : FeatureWorker {
        int top_n_, min_size_;
        ContinentWorker(int tn, int ms) : top_n_(tn), min_size_(ms) {}
        void apply(TileMap& map, WorldFeatures& features) override {
            auto is_land = [](uint16_t t) { return !get_default_registry().is_water(t); };
            auto comps = find_components(map, is_land);
            std::stable_sort(comps.begin(), comps.end(),
                [](const auto& a, const auto& b){ return a.size() > b.size(); });
            int counter = 0;
            for (auto& comp : comps) {
                if (++counter > top_n_) break;
                if (static_cast<int>(comp.size()) < min_size_) break;
                Hex ctr = centroid(comp);
                std::string name = "Continent #" + std::to_string(counter);
                // Continent 是純標籤層，不覆蓋已有的 feature_id
                features.add("Continent", name, comp, ctr);
            }
        }
    };
    return std::make_unique<ContinentWorker>(top_n, min_size);
}

std::unique_ptr<FeatureWorker> make_icecap_worker(float lat_threshold) {
    struct IcecapWorker : FeatureWorker {
        float lat_thresh_;
        explicit IcecapWorker(float t) : lat_thresh_(t) {}
        void apply(TileMap& map, WorldFeatures& features) override {
            const int H = map.height();
            std::vector<Hex> tiles;
            map.for_each([&](const Hex& h, const Tile& tile) {
                if (tile.terrain != TerrainType::SNOW) return;
                float half = std::max((H - 1) / 2.0f, 1e-9f);
                float lat = std::abs(h.r - (H - 1) / 2.0f) / half;
                if (lat >= lat_thresh_) tiles.push_back(h);
            });
            if (tiles.empty()) return;
            Hex ctr = centroid(tiles);
            auto& f = features.add("Icecap", "Icecap #1", tiles, ctr);
            assign_feature_id(map, f.tiles, f.id);
        }
    };
    return std::make_unique<IcecapWorker>(lat_threshold);
}

// ── 主入口 ─────────────────────────────────────────────────────────────────

std::shared_ptr<WorldFeatures> apply_features(
    TileMap& tile_map,
    const std::vector<std::unique_ptr<FeatureWorker>>* workers)
{
    auto wf = std::make_shared<WorldFeatures>();

    if (workers) {
        for (auto& w : *workers) w->apply(tile_map, *wf);
    } else {
        // 預設 worker 集合（對齊 Python apply_features 的 default workers）
        auto run = [&](std::unique_ptr<FeatureWorker> w) { w->apply(tile_map, *wf); };
        run(make_ocean_worker());
        run(make_lake_worker());
        run(make_coast_worker());
        run(make_mountain_range_worker(3));
        for (uint16_t tid : {
            TerrainType::FOREST, TerrainType::DESERT, TerrainType::TUNDRA,
            TerrainType::GRASSLAND, TerrainType::SNOW
        }) run(make_biome_region_worker(tid, 5));
        run(make_island_worker(3));
        run(make_continent_worker(3, 20));
        run(make_icecap_worker(0.85f));
    }
    return wf;
}

} // namespace mapcore

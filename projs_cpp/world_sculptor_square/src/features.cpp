#include "mapcore/features.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace mapcore {

// ── WorldFeatures ─────────────────────────────────────────────────────────

WorldFeature& WorldFeatures::add(
    const std::string& feature_type,
    const std::string& name,
    std::vector<Coord> tiles,
    const Coord& center)
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

static std::vector<std::vector<Coord>> find_components(
    const TileMap& map,
    std::function<bool(uint16_t)> predicate)
{
    const int W = map.width();
    const int H = map.height();
    std::vector<bool> visited(W * H, false);
    std::vector<std::vector<Coord>> result;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = y * W + x;
            if (visited[idx]) continue;
            if (!predicate(map.tile_at(x, y).terrain)) { visited[idx] = true; continue; }
            std::vector<Coord> comp;
            std::vector<Coord> stk = {{x, y}};
            visited[idx] = true;
            while (!stk.empty()) {
                Coord cur = stk.back(); stk.pop_back();
                comp.push_back(cur);
                for (const auto& d : DIRECTIONS) {
                    Coord nb = cur + d;
                    if (!map.in_bounds(nb)) continue;
                    int ni = nb.y * W + nb.x;
                    if (visited[ni]) continue;
                    visited[ni] = true;
                    if (predicate(map.tile_at(nb.x, nb.y).terrain)) stk.push_back(nb);
                }
            }
            result.push_back(std::move(comp));
        }
    }
    return result;
}

static Coord centroid(const std::vector<Coord>& tiles) {
    if (tiles.empty()) return {};
    float x_avg = 0, y_avg = 0;
    for (const auto& c : tiles) { x_avg += c.x; y_avg += c.y; }
    x_avg /= tiles.size(); y_avg /= tiles.size();
    int bx = tiles[0].x, by = tiles[0].y;
    float best = 1e18f;
    for (const auto& c : tiles) {
        float d = std::abs(c.x - x_avg) + std::abs(c.y - y_avg);
        if (d < best) { best = d; bx = c.x; by = c.y; }
    }
    return {bx, by};
}

static void assign_feature_id(TileMap& map, const std::vector<Coord>& tiles, int fid) {
    for (const auto& c : tiles) {
        Tile* t = map.get(c);
        if (t) t->feature_id = fid;
    }
}

// ── 內建 Worker 實作 ──────────────────────────────────────────────────────

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
            Coord ctr = centroid(comp);
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
                bool on_edge = false;
                for (const auto& c : comp) {
                    if (c.x == 0 || c.x == W-1 || c.y == 0 || c.y == H-1) { on_edge = true; break; }
                }
                if (on_edge) continue;
                Coord ctr = centroid(comp);
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
                Coord ctr = centroid(comp);
                std::string name = "Continent #" + std::to_string(counter);
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
            std::vector<Coord> tiles;
            map.for_each([&](const Coord& c, const Tile& tile) {
                if (tile.terrain != TerrainType::SNOW) return;
                float half = std::max((H - 1) / 2.0f, 1e-9f);
                float lat = std::abs(c.y - (H - 1) / 2.0f) / half;
                if (lat >= lat_thresh_) tiles.push_back(c);
            });
            if (tiles.empty()) return;
            Coord ctr = centroid(tiles);
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

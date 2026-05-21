#pragma once
// WorldFeature 命名大區域系統。對齊 RimWorld WorldFeature / WorldGenStep_Features。
// 每個 Feature 對應一塊 tile 集合，有一個 feature_type 字串與顯示名稱。
// Tile.feature_id 直接索引 WorldFeatures::features_[]（-1 = 不屬於任何 feature）。

#include "mapcore/map.hpp"
#include <memory>
#include <string>
#include <vector>

namespace mapcore {

struct WorldFeature {
    int         id{-1};
    std::string feature_type;
    std::string name;
    std::vector<Coord> tiles;
    Coord       center{0, 0};
    int         size{0};
};

struct WorldFeatures {
    std::vector<WorldFeature> features;

    WorldFeature& add(const std::string& feature_type,
                      const std::string& name,
                      std::vector<Coord> tiles,
                      const Coord& center);
    [[nodiscard]] const WorldFeature* get(int feature_id) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return features.size(); }
};

class FeatureWorker {
public:
    virtual ~FeatureWorker() = default;
    virtual void apply(TileMap& map, WorldFeatures& features) = 0;
};

// ── 內建 Worker 工廠函式 ───────────────────────────────────────────────────

std::unique_ptr<FeatureWorker> make_ocean_worker();
std::unique_ptr<FeatureWorker> make_lake_worker();
std::unique_ptr<FeatureWorker> make_coast_worker();
std::unique_ptr<FeatureWorker> make_mountain_range_worker(int min_size = 3);
std::unique_ptr<FeatureWorker> make_biome_region_worker(uint16_t terrain_id, int min_size = 5);
std::unique_ptr<FeatureWorker> make_island_worker(int min_size = 3);
std::unique_ptr<FeatureWorker> make_continent_worker(int top_n = 3, int min_size = 20);
std::unique_ptr<FeatureWorker> make_icecap_worker(float lat_threshold = 0.85f);

std::shared_ptr<WorldFeatures> apply_features(
    TileMap& tile_map,
    const std::vector<std::unique_ptr<FeatureWorker>>* workers = nullptr
);

} // namespace mapcore

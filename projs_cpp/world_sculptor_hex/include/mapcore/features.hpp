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
    std::string feature_type;  // e.g. "Ocean", "MountainRange", "BiomeRegion:FOREST"
    std::string name;          // 顯示名稱（placeholder "<type> #N"）
    std::vector<Hex> tiles;
    Hex         center{0, 0};  // axial 重心（最近成員），供地圖標籤定位
    int         size{0};
};

// WorldFeatures：list 索引容器；feature_id == list index
struct WorldFeatures {
    std::vector<WorldFeature> features;

    WorldFeature& add(const std::string& feature_type,
                      const std::string& name,
                      std::vector<Hex> tiles,
                      const Hex& center);
    [[nodiscard]] const WorldFeature* get(int feature_id) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return features.size(); }
};

// FeatureWorker 抽象基類：每種 worker 定義一類 feature 的識別與生成邏輯。
class FeatureWorker {
public:
    virtual ~FeatureWorker() = default;
    virtual void apply(TileMap& map, WorldFeatures& features) = 0;
};

// ── 內建 Worker 工廠函式 ───────────────────────────────────────────────────

// 直接新增連通分量 features（每個連通塊 = 一個 feature）
std::unique_ptr<FeatureWorker> make_ocean_worker();
std::unique_ptr<FeatureWorker> make_lake_worker();
std::unique_ptr<FeatureWorker> make_coast_worker();
std::unique_ptr<FeatureWorker> make_mountain_range_worker(int min_size = 3);

// 生物群系大區域（同一生物群系 terrain 的連通塊）
std::unique_ptr<FeatureWorker> make_biome_region_worker(uint16_t terrain_id, int min_size = 5);

// 島嶼：陸地連通塊（不觸及地圖邊界）
std::unique_ptr<FeatureWorker> make_island_worker(int min_size = 3);

// 大陸：最大幾個陸地連通塊（可與其他 feature 共存；以 continent_label tag 標記）
std::unique_ptr<FeatureWorker> make_continent_worker(int top_n = 3, int min_size = 20);

// 極冠：緯度 >= threshold 且 terrain=SNOW 的格子
std::unique_ptr<FeatureWorker> make_icecap_worker(float lat_threshold = 0.85f);

// ── 主入口 ─────────────────────────────────────────────────────────────────

// 以 workers 列表跑完所有 phase，回傳 WorldFeatures（同時寫入 Tile.feature_id）。
// workers=nullptr 時使用內建預設集合。
std::shared_ptr<WorldFeatures> apply_features(
    TileMap& tile_map,
    const std::vector<std::unique_ptr<FeatureWorker>>* workers = nullptr
);

} // namespace mapcore

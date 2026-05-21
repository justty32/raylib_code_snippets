#pragma once
// 一站式地圖生成管線（Phase 1~6）。
// generate_world() 回傳 WorldGenResult（對應 Python 版 WorldGenResult dataclass）。

#include "mapcore/map.hpp"
#include "mapcore/features.hpp"
#include "mapcore/rivers.hpp"
#include "mapcore/generation/heightmap.hpp"
#include "mapcore/generation/biome.hpp"
#include "mapcore/generation/climate.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mapcore {
namespace generation {

// 所有中間產物集中在此，overlay phase 只需接收這一個物件。
// None/nullopt 代表對應 phase 未執行。
struct WorldGenResult {
    TileMap                tile_map;
    std::vector<float>     heightmap;     // flat W*H
    std::vector<float>     moisture;      // flat W*H
    std::vector<float>     temperature_celsius;  // empty = climate 未執行
    std::vector<float>     rainfall_mm;          // empty = climate 未執行
    std::unordered_map<std::string, std::vector<float>> extra_noise;  // 供 overlay 用
    TerrainRegistry*       registry{nullptr};  // 指向呼叫端使用的 registry
    std::optional<uint64_t> seed;

    // 方便判斷 climate 是否執行
    bool has_climate() const noexcept { return !temperature_celsius.empty(); }
};

// seed offset 常數（對齊 Python 版）
constexpr uint64_t MOISTURE_SEED_OFFSET       = 99991ULL;
constexpr uint64_t RIVERS_SEED_OFFSET         = 314159ULL;
constexpr uint64_t CLIMATE_SEED_OFFSET        = 271828ULL;
constexpr uint64_t EXTRA_NOISE_BASE_OFFSET    = 500003ULL;

struct WorldGenParams {
    // 地圖尺寸
    float sea_level   = 0.4f;
    int   coast_depth = 1;

    // noise（同時套用到 heightmap 與 moisture）
    int   octaves         = 5;
    float persistence     = 0.5f;
    int   base_frequency  = 4;

    // heightmap 形狀與山脊（透傳給 HeightmapParams）
    HeightmapParams heightmap_params;

    // biome
    BiomeParams biome_params;

    // post-process (Phase 4)
    bool     post_process    = true;
    int      island_min_size = 3;
    int      lake_max_size   = 4;
    uint16_t lake_fill       = TerrainType::PLAINS;

    // 窪地填充 (Phase 4.5)
    bool lake_depressions = false;

    // climate (Phase 5)
    bool         climate = true;
    ClimateParams climate_params;

    // rivers（RimWorld 風）
    bool rivers = true;
    RiverGenParams river_params;

    // features (Phase 6)
    bool features = true;
    const std::vector<std::unique_ptr<FeatureWorker>>* feature_workers = nullptr;

    // registry（nullptr = DEFAULT_REGISTRY）
    TerrainRegistry* registry = nullptr;

    // extra_noise_specs: (name, seed_offset)
    std::vector<std::pair<std::string, int>> extra_noise_specs;
};

// 跑完 Phase 1~6，回傳 WorldGenResult。
[[nodiscard]] WorldGenResult generate_world(
    int width,
    int height,
    std::optional<uint64_t> seed = std::nullopt,
    const WorldGenParams& params  = {}
);

} // namespace generation
} // namespace mapcore

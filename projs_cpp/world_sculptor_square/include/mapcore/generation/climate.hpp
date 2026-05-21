#pragma once
// 氣候階段 (Phase 5)：temperature(°C) / rainfall(mm) / Hilliness。
// 對齊 RimWorld WorldGenStep_Terrain.cs 的計算方式。

#include "mapcore/map.hpp"
#include <optional>
#include <vector>

namespace mapcore {
namespace generation {

struct ClimateParams {
    float sea_level              = 0.4f;
    float temperature_offset_amp = 4.0f;   // ±°C noise 擾動幅度
    float hill_threshold         = 0.70f;
    float mountain_threshold     = 0.85f;
    float impassable_threshold   = 0.95f;
    float rain_shadow_strength   = 0.0f;   // 0=關閉雨影
};

struct ClimateResult {
    std::vector<float> temperature_celsius;  // flat W*H
    std::vector<float> rainfall_mm;          // flat W*H
};

// 對 tile_map 算 temperature(°C) / rainfall(mm) / Tile.hilliness。
// rainfall_noise: 0~1 的 base noise（建議傳 moisture grid）。
// 回傳 ClimateResult；同時 in-place 寫入 Tile.hilliness。
[[nodiscard]] ClimateResult apply_climate(
    TileMap& tile_map,
    const std::vector<float>& heightmap,
    const std::vector<float>& rainfall_noise,
    std::optional<uint64_t> seed = std::nullopt,
    const ClimateParams& params  = {}
);

// 個別計算工具（供外部使用）
[[nodiscard]] float latitude_normalized(int r, int total_h) noexcept;
[[nodiscard]] float base_temperature_celsius(float lat_norm) noexcept;
[[nodiscard]] float compute_temperature_celsius(int r, int total_h, float elev,
                                                float noise_offset = 0.0f) noexcept;
[[nodiscard]] float compute_rainfall_mm(int r, int total_h, float elev,
                                        float base_noise) noexcept;
[[nodiscard]] Hilliness compute_hilliness(float elev, float sea_level,
                                          float hill_threshold, float mountain_threshold,
                                          float impassable_threshold,
                                          float random01 = -1.0f) noexcept;

} // namespace generation
} // namespace mapcore

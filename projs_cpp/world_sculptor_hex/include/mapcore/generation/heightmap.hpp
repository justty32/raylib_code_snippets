#pragma once
// 高程 noise (Phase 1)。
// 多層 value noise (fBm-like) + smoothstep + bilinear 插值。
// ridge_mode="plates"（預設）以 Voronoi 板塊局部化山脊。
// 支援 6 種地圖形狀遮罩 (island / archipelago / pangaea / continents / ring_sea / shattered_archipelago)。
// 回傳 flat std::vector<float>，size = width * height，索引 r*width+q。

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mapcore {
namespace generation {

struct HeightmapParams {
    int   octaves                         = 4;
    float persistence                     = 0.5f;
    int   base_frequency                  = 4;
    // ridge
    float ridge_weight                    = 0.0f;
    std::string ridge_mode               = "plates";  // "plates" | "global"
    float ridge_direction                 = 0.0f;     // global 模式用
    float ridge_direction_variation       = 90.0f;    // global 模式用
    float ridge_power                     = 2.0f;
    float ridge_multifractal_gain         = 2.0f;
    int   num_plates                      = 20;
    float plate_boundary_width            = 0.05f;
    // shape
    std::string shape;                               // "" = 無遮罩
    float shape_strength                  = 0.85f;
    std::unordered_map<std::string, float> shape_params;
    float shape_sea_level                 = 0.4f;
};

// 產生 height × width 的高程陣列，值 ∈ [0, 1]，flat row-major（索引 r*width+q）。
[[nodiscard]] std::vector<float> generate_heightmap(
    int width,
    int height,
    std::optional<uint64_t> seed = std::nullopt,
    const HeightmapParams& params = {}
);

} // namespace generation
} // namespace mapcore

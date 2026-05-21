#pragma once
// 窪地填充 (Phase 4.5)：Priority-Flood 演算法（Barnes et al. 2014 簡化版）。
// 識別 heightmap 中無法排水到海的內陸窪地，產生湖泊格。

#include <set>
#include <utility>
#include <vector>

namespace mapcore {
namespace generation {

struct DepressionResult {
    std::vector<std::pair<int,int>> lake_tiles;  // (r, q) 湖格
    std::vector<float> filled;                   // 填充後高程，flat row-major
};

// heightmap: flat row-major W*H，值 ∈ [0, 1]。
[[nodiscard]] DepressionResult fill_depressions(
    const std::vector<float>& heightmap,
    int width, int height,
    float sea_level = 0.4f
);

} // namespace generation
} // namespace mapcore

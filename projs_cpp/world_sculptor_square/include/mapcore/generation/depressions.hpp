#pragma once
// 窪地填充 (Phase 4.5)：Priority-Flood 演算法（Barnes et al. 2014 簡化版）。
// 識別 heightmap 中無法排水到海的內陸窪地，產生湖泊格。
// 方格 4 鄰居版（von Neumann）。

#include "mapcore/grid.hpp"
#include <utility>
#include <vector>

namespace mapcore {
namespace generation {

struct DepressionResult {
    std::vector<Coord> lake_tiles;  // 湖格
    std::vector<float> filled;      // 填充後高程，flat row-major
};

[[nodiscard]] DepressionResult fill_depressions(
    const std::vector<float>& heightmap,
    int width, int height,
    float sea_level = 0.4f
);

} // namespace generation
} // namespace mapcore

#pragma once
// 海平面切割 (Phase 2)。
// heightmap → TileMap: OCEAN / COAST / PLAINS。
// coast_depth 圈的 BFS 擴張（OCEAN → COAST）。

#include "mapcore/map.hpp"
#include <vector>

namespace mapcore {
namespace generation {

// 以 sea_level 切割 heightmap，回傳含 OCEAN / COAST / PLAINS 的 TileMap。
// heightmap: flat row-major vector（size = width * height，索引 r*W+q）。
[[nodiscard]] TileMap heightmap_to_tilemap(
    const std::vector<float>& heightmap,
    int width, int height,
    float sea_level   = 0.4f,
    int   coast_depth = 1
);

// 從現有 OCEAN 出發擴張 coast_depth 圈 COAST。
void expand_coast(TileMap& tile_map, int coast_depth);

} // namespace generation
} // namespace mapcore

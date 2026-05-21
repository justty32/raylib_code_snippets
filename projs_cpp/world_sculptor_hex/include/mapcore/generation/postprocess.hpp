#pragma once
// 地圖後處理 (Phase 4)：連通分量 BFS + 清島 + 填湖 + 重標 COAST。

#include "mapcore/map.hpp"
#include <functional>
#include <vector>

namespace mapcore {
namespace generation {

// BFS 連通分量；回傳每個分量的 tiles list。
[[nodiscard]] std::vector<std::vector<Hex>> find_components(
    const TileMap& tile_map,
    std::function<bool(uint16_t)> predicate
);

// 清掉 size < min_size 的陸地連通分量（淹成 OCEAN）；回傳被淹格數。
int remove_small_islands(TileMap& tile_map, int min_size = 3);

// 填掉 size <= max_size 且不接地圖邊界的水體；回傳被填格數。
int remove_small_lakes(TileMap& tile_map, int max_size = 4,
                       uint16_t fill = TerrainType::PLAINS);

// 重標 COAST：先還原 OCEAN，再重跑 expand_coast。
void relabel_coast(TileMap& tile_map, int coast_depth = 1);

struct PostProcessResult {
    int islands_removed{0};
    int lakes_filled{0};
};

// Phase 4 完整流程：清島 → 填湖 → 重標 COAST。
PostProcessResult post_process(
    TileMap& tile_map,
    int island_min_size = 3,
    int lake_max_size   = 4,
    int coast_depth     = 1,
    uint16_t lake_fill  = TerrainType::PLAINS
);

} // namespace generation
} // namespace mapcore

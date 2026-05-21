#pragma once
// 地圖後處理 (Phase 4)：連通分量 BFS + 清島 + 填湖 + 重標 COAST。

#include "mapcore/map.hpp"
#include <functional>
#include <vector>

namespace mapcore {
namespace generation {

[[nodiscard]] std::vector<std::vector<Coord>> find_components(
    const TileMap& tile_map,
    std::function<bool(uint16_t)> predicate
);

int remove_small_islands(TileMap& tile_map, int min_size = 3);

int remove_small_lakes(TileMap& tile_map, int max_size = 4,
                       uint16_t fill = TerrainType::PLAINS);

void relabel_coast(TileMap& tile_map, int coast_depth = 1);

struct PostProcessResult {
    int islands_removed{0};
    int lakes_filled{0};
};

PostProcessResult post_process(
    TileMap& tile_map,
    int island_min_size = 3,
    int lake_max_size   = 4,
    int coast_depth     = 1,
    uint16_t lake_fill  = TerrainType::PLAINS
);

} // namespace generation
} // namespace mapcore

#pragma once
// A* 尋路（4 鄰居版）。
// 設計對齊 Python mapcore_py_square：g_score / came_from / closed 全部用 flat 2D array。
// heuristic = grid_distance × MIN_PASSABLE_COST（Manhattan，admissible）。
// river_crossing_cost：跨越河流邊每點流量加的成本（0=不影響）。

#include "mapcore/map.hpp"
#include <optional>
#include <vector>

namespace mapcore {

[[nodiscard]] float get_min_passable_cost();

[[nodiscard]] std::optional<std::vector<Coord>> astar(
    const TileMap& tile_map,
    const Coord& start,
    const Coord& goal,
    float river_crossing_cost = 0.0f
);

[[nodiscard]] float path_cost(const TileMap& tile_map, const std::vector<Coord>& path);

} // namespace mapcore

#pragma once
// A* 尋路。
// 設計對齊 Python 版：g_score / came_from / closed 全部用 flat 2D array（std::vector）。
// heuristic = hex_distance × MIN_PASSABLE_COST（確保 admissibility）。
// river_crossing_cost：跨越河流邊每點流量加的成本（0=不影響）。

#include "mapcore/map.hpp"
#include <optional>
#include <vector>

namespace mapcore {

// 最便宜可通行地形的成本；由 get_min_passable_cost() 從 DEFAULT_REGISTRY 計算。
[[nodiscard]] float get_min_passable_cost();

// A*：從 start 到 goal 的最短路徑（含兩端點）。不可達回 std::nullopt。
[[nodiscard]] std::optional<std::vector<Hex>> astar(
    const TileMap& tile_map,
    const Hex& start,
    const Hex& goal,
    float river_crossing_cost = 0.0f
);

// 計算已知路徑的總成本（進入每個非起點格的 terrain_cost 之和）。
[[nodiscard]] float path_cost(const TileMap& tile_map, const std::vector<Hex>& path);

} // namespace mapcore

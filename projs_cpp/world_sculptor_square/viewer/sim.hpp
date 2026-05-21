#pragma once
// 編輯器內建模擬：漫水（海洋遮罩）與氣候（溫度 / 降雨）。
// 對應 mapcore_py_square/editor/sim/hydrology.py 與 climate.py。

#include "editor_state.hpp"
#include <string>

namespace viewer {

// BFS 漫水：從水源（或邊界低格）出發，標記 EditorState::ocean_mask。
void run_flood_fill(EditorState& s);

// 太陽軌跡 + 風向 + 雨影 → temperature / rainfall。
// 需先呼叫 run_flood_fill() 以確保 ocean_mask 有效。
void run_climate(EditorState& s);

// 從 EditorState heightmap 跑 pipeline（classify → biome），
// 存成 exported_world_square.json，回傳狀態字串。
[[nodiscard]] std::string export_world(const EditorState& s);

} // namespace viewer

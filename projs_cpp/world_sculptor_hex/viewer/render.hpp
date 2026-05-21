#pragma once
// 高程著色：port 自 mapcore_py/editor/app.py 的 _HEIGHT_BANDS / _height_color。
// viewer 不做 flood-fill，所以「是否海洋」直接以 h < sea_level 判斷
// （Python 版另有 ocean_mask 區分內陸湖窪地，這裡先不分）。

#include "raylib.h"
#include "editor_state.hpp"

namespace viewer {

// 高程 → 顏色。h ∈ [0,1]，sea_level 預設 0.35。
[[nodiscard]] Color height_color(float h, float sea_level) noexcept;

// 整張地圖渲染（含 viewport culling、水源 marker）。
void draw_world(const EditorState& s, const Camera2D& camera);

} // namespace viewer

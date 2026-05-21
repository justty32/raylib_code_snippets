#pragma once
// 高程著色：port 自 mapcore_cpp/viewer/render.hpp。
// 方格版：每格用 DrawRectangle 而非 DrawPoly(6,...)。

#include "raylib.h"
#include "editor_state.hpp"

namespace viewer {

[[nodiscard]] Color height_color(float h, float sea_level) noexcept;

void draw_world(const EditorState& s, const Camera2D& camera);

} // namespace viewer

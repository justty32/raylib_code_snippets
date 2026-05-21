#pragma once
// 地形雕刻工具：對應 mapcore_cpp/viewer/tools.hpp 的方格版本。
//
// 注意：radial stamp 用 std::mt19937 而非 raylib GetRandomValue() —
// raylib RNG 沒有公開的 uniform 浮點介面，且 viewer 在多筆刷觸發間需要
// 自帶 state，避免被 raylib 內部偷偷重置。

#include "editor_state.hpp"

#include <random>

namespace viewer {

class ToolRng {
public:
    ToolRng() : engine_(std::random_device{}()) {}
    void   seed(unsigned s) noexcept { engine_.seed(s); }
    [[nodiscard]] int   uniform_int  (int lo, int hi);
    [[nodiscard]] float uniform_float(float lo, float hi);
private:
    std::mt19937 engine_;
};

void apply_brush(EditorState& s, int x, int y, float delta);
void apply_ridge_stamp(EditorState& s, int x, int y, ToolRng& rng);
void apply_rift_stamp(EditorState& s, int x, int y, ToolRng& rng);
void toggle_water_source(EditorState& s, int x, int y);

} // namespace viewer

#pragma once
// 地形雕刻工具：對應 mapcore_py/editor/tools.py。
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

// 高斯衰減筆刷（中心 delta 最強，邊緣趨近 0）。
void apply_brush(EditorState& s, int q, int r, float delta);

// 徑向山脊筆刷（中心高、放射線可選）。需要 RNG 來決定 spokes_rand。
void apply_ridge_stamp(EditorState& s, int q, int r, ToolRng& rng);

// 徑向裂谷筆刷（中心低）。
void apply_rift_stamp(EditorState& s, int q, int r, ToolRng& rng);

// 切換水源 marker：存在則移除，不存在則加入。
void toggle_water_source(EditorState& s, int q, int r);

} // namespace viewer

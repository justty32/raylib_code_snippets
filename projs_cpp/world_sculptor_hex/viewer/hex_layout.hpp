#pragma once
// Hex 像素佈局工具：對應 mapcore_py/editor/hex_layout.py。
// 採 odd-r offset 座標系（pointy-top hex；偶數列不偏移、奇數列向右偏移半格）。
// 與 mapcore::Hex（axial）語意不同；viewer 統一用 offset，與 Python editor 對齊。
//
// 注意：座標皆為 world-space；pan/zoom 由 raylib Camera2D 處理，
// 不在這層加 ox/oy 偏移。

#include "raylib.h"
#include <array>
#include <vector>

namespace viewer {

inline constexpr float kSqrt3 = 1.7320508075688772f;

struct HexCoord {
    int q;
    int r;
    bool operator==(const HexCoord& o) const noexcept { return q == o.q && r == o.r; }
};

// odd-r offset → 像素中心（world-space）
[[nodiscard]] Vector2 hex_to_pixel(int q, int r, float size) noexcept;

// 像素 (world-space) → odd-r offset (q, r)
[[nodiscard]] HexCoord pixel_to_hex(float px, float py, float size) noexcept;

// pointy-top hex 六頂點（順時針）
[[nodiscard]] std::array<Vector2, 6> hex_corners(Vector2 center, float size) noexcept;

// hex 距離
[[nodiscard]] int hex_distance(int q1, int r1, int q2, int r2) noexcept;

// 沿直線收集所有 hex（含兩端點）
[[nodiscard]] std::vector<HexCoord> hex_line(int q0, int r0, int q1, int r1);

// 半徑 radius 圓盤內所有 hex
[[nodiscard]] std::vector<HexCoord> hex_disk(int cq, int cr, int radius);

// 計算容納整張 width×height 地圖所需的 world-space 像素尺寸（用於初始 camera 對齊）
[[nodiscard]] Vector2 canvas_pixel_size(int width, int height, float size, float margin = 30.0f) noexcept;

} // namespace viewer

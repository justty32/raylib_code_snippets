#pragma once
// 方格像素佈局工具：對應 mapcore_cpp/viewer/hex_layout.hpp 的方格版本。
// 採 (x, y) 直角座標；y 向下與 raylib 螢幕座標一致。
//
// 與 mapcore::Coord 同樣是 (int x, int y)；這裡的 GridCoord 是 viewer 內部型別，
// 不直接依賴 mapcore lib，避免 viewer 升級時跟著動 lib API。

#include "raylib.h"
#include <array>
#include <vector>

namespace viewer {

struct GridCoord {
    int x;
    int y;
    bool operator==(const GridCoord& o) const noexcept { return x == o.x && y == o.y; }
};

// cell (x, y) 中心 → 像素中心（world-space）
[[nodiscard]] Vector2 grid_to_pixel(int x, int y, float cell_size) noexcept;

// 像素 (world-space) → cell (x, y)
[[nodiscard]] GridCoord pixel_to_grid(float px, float py, float cell_size) noexcept;

// cell 的四個角（NE, NW, SW, SE 順時針），給 outline 用
[[nodiscard]] std::array<Vector2, 4> grid_corners(Vector2 center, float cell_size) noexcept;

// Chebyshev 距離 = max(|dx|, |dy|)；給編輯器筆刷使用，符合「方形 N-ring」直覺。
[[nodiscard]] int grid_chebyshev(int x1, int y1, int x2, int y2) noexcept;

// Manhattan 距離 = |dx|+|dy|；對齊 mapcore::grid_distance（4 連通最短步數）。
[[nodiscard]] int grid_manhattan(int x1, int y1, int x2, int y2) noexcept;

// 沿直線收集所有 cell（含兩端點）。Bresenham supercover：相鄰 cell 必為 4-鄰接，
// 長度 = Manhattan + 1。
[[nodiscard]] std::vector<GridCoord> grid_line(int x0, int y0, int x1, int y1);

// Chebyshev 圓盤：max(|dx|,|dy|) ≤ radius，共 (2r+1)² 格。筆刷預覽用。
[[nodiscard]] std::vector<GridCoord> grid_disk(int cx, int cy, int radius);

// Manhattan 環：|dx|+|dy| == radius，共 4r 格（r=0 → 1 格）。
// 對應 mapcore::grid_ring，給「庫端等距」呈現用，目前 viewer 沒用。
[[nodiscard]] std::vector<GridCoord> grid_diamond_ring(int cx, int cy, int radius);

// 計算容納整張 width×height 地圖所需的 world-space 像素尺寸（用於初始 camera 對齊）
[[nodiscard]] Vector2 canvas_pixel_size(int width, int height, float cell_size,
                                        float margin = 30.0f) noexcept;

} // namespace viewer

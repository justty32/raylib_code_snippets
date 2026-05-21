#include "grid_layout.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

Vector2 grid_to_pixel(int x, int y, float cell_size) noexcept {
    // 像素中心 = (x*size + size/2, y*size + size/2)
    const float half = cell_size * 0.5f;
    return {static_cast<float>(x) * cell_size + half,
            static_cast<float>(y) * cell_size + half};
}

GridCoord pixel_to_grid(float px, float py, float cell_size) noexcept {
    // floor 即可：cell (x,y) 涵蓋 [x*size, (x+1)*size)
    const float fx = std::floor(px / cell_size);
    const float fy = std::floor(py / cell_size);
    return {static_cast<int>(fx), static_cast<int>(fy)};
}

std::array<Vector2, 4> grid_corners(Vector2 center, float cell_size) noexcept {
    const float h = cell_size * 0.5f;
    // 順序 NE, NW, SW, SE（順時針從右上）；對齊 EDGE_CORNERS 在 rivers 中的約定
    return {{
        {center.x + h, center.y - h},  // NE
        {center.x - h, center.y - h},  // NW
        {center.x - h, center.y + h},  // SW
        {center.x + h, center.y + h},  // SE
    }};
}

int grid_chebyshev(int x1, int y1, int x2, int y2) noexcept {
    return std::max(std::abs(x1 - x2), std::abs(y1 - y2));
}

int grid_manhattan(int x1, int y1, int x2, int y2) noexcept {
    return std::abs(x1 - x2) + std::abs(y1 - y2);
}

std::vector<GridCoord> grid_line(int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    if (dx == 0 && dy == 0) return {{x0, y0}};
    const int sx = (x1 > x0) ? 1 : -1;
    const int sy = (y1 > y0) ? 1 : -1;
    std::vector<GridCoord> out;
    out.reserve(static_cast<size_t>(dx + dy + 1));
    int error = dx - dy;
    int x = x0, y = y0;
    out.push_back({x, y});
    for (int i = 0; i < dx + dy; ++i) {
        const int e2 = 2 * error;
        if (e2 > -dy) { error -= dy; x += sx; }
        else          { error += dx; y += sy; }
        out.push_back({x, y});
    }
    return out;
}

std::vector<GridCoord> grid_disk(int cx, int cy, int radius) {
    if (radius < 0) return {{cx, cy}};
    std::vector<GridCoord> out;
    out.reserve(static_cast<size_t>((2 * radius + 1) * (2 * radius + 1)));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            out.push_back({cx + dx, cy + dy});
        }
    }
    return out;
}

std::vector<GridCoord> grid_diamond_ring(int cx, int cy, int radius) {
    if (radius <= 0) return {{cx, cy}};
    std::vector<GridCoord> out;
    out.reserve(static_cast<size_t>(4 * radius));
    int x = cx - radius;
    int y = cy;
    constexpr int sides[4][2] = {{1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
    for (auto& s : sides) {
        for (int i = 0; i < radius; ++i) {
            out.push_back({x, y});
            x += s[0];
            y += s[1];
        }
    }
    return out;
}

Vector2 canvas_pixel_size(int width, int height, float cell_size, float margin) noexcept {
    const float max_x = static_cast<float>(width)  * cell_size + margin * 2.0f;
    const float max_y = static_cast<float>(height) * cell_size + margin * 2.0f;
    return {max_x, max_y};
}

} // namespace viewer

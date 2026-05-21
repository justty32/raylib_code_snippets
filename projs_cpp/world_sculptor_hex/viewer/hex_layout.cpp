#include "hex_layout.hpp"

#include <cmath>

namespace viewer {

namespace {

struct Cube { int x, y, z; };

[[nodiscard]] Cube to_cube(int q, int r) noexcept {
    const int x = q - (r - (r & 1)) / 2;
    const int z = r;
    const int y = -x - z;
    return {x, y, z};
}

[[nodiscard]] HexCoord from_cube(int x, int /*y*/, int z) noexcept {
    const int q = x + (z - (z & 1)) / 2;
    const int r = z;
    return {q, r};
}

[[nodiscard]] Cube cube_round(float xf, float yf, float zf) noexcept {
    int rx = static_cast<int>(std::lround(xf));
    int ry = static_cast<int>(std::lround(yf));
    int rz = static_cast<int>(std::lround(zf));
    const float dx = std::abs(rx - xf);
    const float dy = std::abs(ry - yf);
    const float dz = std::abs(rz - zf);
    if (dx > dy && dx > dz)       rx = -ry - rz;
    else if (dy > dz)              ry = -rx - rz;
    else                           rz = -rx - ry;
    return {rx, ry, rz};
}

} // anonymous

Vector2 hex_to_pixel(int q, int r, float size) noexcept {
    const float x = size * kSqrt3 * (static_cast<float>(q) + 0.5f * (r & 1));
    const float y = size * 1.5f * static_cast<float>(r);
    return {x, y};
}

HexCoord pixel_to_hex(float px, float py, float size) noexcept {
    const float lx = px / size;
    const float ly = py / size;
    // Pointy-top axial inverse (Red Blob)
    const float qf_ax = (kSqrt3 / 3.0f) * lx - (1.0f / 3.0f) * ly;
    const float rf_ax = (2.0f / 3.0f) * ly;
    // axial → cube
    const float xf = qf_ax;
    const float zf = rf_ax;
    const float yf = -xf - zf;
    const Cube c = cube_round(xf, yf, zf);
    return from_cube(c.x, c.y, c.z);
}

std::array<Vector2, 6> hex_corners(Vector2 center, float size) noexcept {
    // 對齊 hex_layout.py：角度 = 60°·i - 30°，sin 在螢幕座標系下「向下為正」
    std::array<Vector2, 6> out{};
    for (int i = 0; i < 6; ++i) {
        const float angle = static_cast<float>(60 * i - 30) * 0.017453292519943295f; // DEG2RAD
        out[i].x = center.x + size * std::cos(angle);
        out[i].y = center.y + size * std::sin(angle);
    }
    return out;
}

int hex_distance(int q1, int r1, int q2, int r2) noexcept {
    const auto a = to_cube(q1, r1);
    const auto b = to_cube(q2, r2);
    return (std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z)) / 2;
}

std::vector<HexCoord> hex_line(int q0, int r0, int q1, int r1) {
    const int n = hex_distance(q0, r0, q1, r1);
    if (n == 0) return { {q0, r0} };
    const auto a = to_cube(q0, r0);
    const auto b = to_cube(q1, r1);
    std::vector<HexCoord> out;
    out.reserve(static_cast<size_t>(n + 1));
    for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float xf = static_cast<float>(a.x) + static_cast<float>(b.x - a.x) * t;
        const float yf = static_cast<float>(a.y) + static_cast<float>(b.y - a.y) * t;
        const float zf = static_cast<float>(a.z) + static_cast<float>(b.z - a.z) * t;
        const Cube c = cube_round(xf, yf, zf);
        out.push_back(from_cube(c.x, c.y, c.z));
    }
    return out;
}

std::vector<HexCoord> hex_disk(int cq, int cr, int radius) {
    const auto c = to_cube(cq, cr);
    std::vector<HexCoord> out;
    out.reserve(static_cast<size_t>(3 * radius * (radius + 1) + 1));
    for (int dx = -radius; dx <= radius; ++dx) {
        const int dy_lo = std::max(-radius, -dx - radius);
        const int dy_hi = std::min( radius, -dx + radius);
        for (int dy = dy_lo; dy <= dy_hi; ++dy) {
            const int dz = -dx - dy;
            out.push_back(from_cube(c.x + dx, c.y + dy, c.z + dz));
        }
    }
    return out;
}

Vector2 canvas_pixel_size(int width, int height, float size, float margin) noexcept {
    const float max_x = size * kSqrt3 * (static_cast<float>(width - 1) + 0.5f) + margin * 2.0f + size * 2.0f;
    const float max_y = size * 1.5f * static_cast<float>(height - 1) + margin * 2.0f + size * 2.0f;
    return {max_x, max_y};
}

} // namespace viewer

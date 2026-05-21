#pragma once
// 方格座標核心 (4-directional von Neumann)。
// 對齊 mapcore_cpp/include/mapcore/hex.hpp 的方格版本：
//   Coord(x, y) 對應 hex Hex(q, r)；4 鄰居 (E/N/W/S) 對應 hex 6 鄰居。
//   rivers 的 edge ownership（slot 0..1 自擁、2..3 鄰居擁）依賴此順序。

#include <array>
#include <vector>
#include <functional>
#include <stdexcept>

namespace mapcore {

struct Coord {
    int x{0}, y{0};

    constexpr Coord() noexcept = default;
    constexpr Coord(int x_, int y_) noexcept : x(x_), y(y_) {}

    constexpr Coord operator+(const Coord& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Coord operator-(const Coord& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Coord operator*(int k) const noexcept { return {x * k, y * k}; }
    constexpr bool  operator==(const Coord& o) const noexcept { return x == o.x && y == o.y; }
    constexpr bool  operator!=(const Coord& o) const noexcept { return !(*this == o); }

    [[nodiscard]] Coord neighbor(int direction_index) const noexcept;
    [[nodiscard]] std::array<Coord, 4> neighbors() const noexcept;
};

// 4 方向順序鎖死：0=E, 1=N, 2=W, 3=S。對立方向 i ↔ (i+2)%4。
// y 向下，所以 N = (0, -1)、S = (0, +1)。
extern const std::array<Coord, 4> DIRECTIONS;

[[nodiscard]] int grid_distance(const Coord& a, const Coord& b) noexcept;
[[nodiscard]] std::vector<Coord> grid_line(const Coord& start, const Coord& end);
[[nodiscard]] std::vector<Coord> grid_ring(const Coord& center, int radius);
[[nodiscard]] std::vector<Coord> grid_spiral(const Coord& center, int max_radius);

} // namespace mapcore

namespace std {
template<>
struct hash<mapcore::Coord> {
    size_t operator()(const mapcore::Coord& c) const noexcept {
        auto uh = static_cast<size_t>(static_cast<unsigned int>(c.x));
        auto ul = static_cast<size_t>(static_cast<unsigned int>(c.y));
        return uh * 0x9E3779B97F4A7C15ULL ^ (ul + 0x9E3779B97F4A7C15ULL + (uh << 6) + (uh >> 2));
    }
};
} // namespace std

#pragma once
// 六角格座標核心 (axial)。
// 方向順序對齊 Python 版：0=E, 1=NE, 2=NW, 3=W, 4=SW, 5=SE
// rivers 模組的 edge ownership（slot 0..2 自擁）依賴此順序，不可變動。

#include <array>
#include <vector>
#include <functional>
#include <stdexcept>

namespace mapcore {

struct Hex {
    int q{0}, r{0};

    constexpr Hex() noexcept = default;
    constexpr Hex(int q_, int r_) noexcept : q(q_), r(r_) {}

    [[nodiscard]] constexpr int s() const noexcept { return -q - r; }

    constexpr Hex operator+(const Hex& o) const noexcept { return {q + o.q, r + o.r}; }
    constexpr Hex operator-(const Hex& o) const noexcept { return {q - o.q, r - o.r}; }
    constexpr Hex operator*(int k) const noexcept { return {q * k, r * k}; }
    constexpr bool operator==(const Hex& o) const noexcept { return q == o.q && r == o.r; }
    constexpr bool operator!=(const Hex& o) const noexcept { return !(*this == o); }

    [[nodiscard]] Hex neighbor(int direction_index) const noexcept;
    [[nodiscard]] std::array<Hex, 6> neighbors() const noexcept;
};

// pointy-top 軸向方向向量；順序固定不可改
extern const std::array<Hex, 6> DIRECTIONS;

[[nodiscard]] int hex_distance(const Hex& a, const Hex& b) noexcept;
[[nodiscard]] Hex hex_round(float qf, float rf) noexcept;
[[nodiscard]] std::vector<Hex> hex_line(const Hex& start, const Hex& end);
[[nodiscard]] std::vector<Hex> hex_ring(const Hex& center, int radius);
[[nodiscard]] std::vector<Hex> hex_spiral(const Hex& center, int max_radius);

} // namespace mapcore

namespace std {
template<>
struct hash<mapcore::Hex> {
    size_t operator()(const mapcore::Hex& h) const noexcept {
        // 把兩個 int 壓進一個 64-bit key；避免 (1,0) 跟 (0,1) 碰撞
        auto uh = static_cast<size_t>(static_cast<unsigned int>(h.q));
        auto ul = static_cast<size_t>(static_cast<unsigned int>(h.r));
        return uh * 0x9E3779B97F4A7C15ULL ^ (ul + 0x9E3779B97F4A7C15ULL + (uh << 6) + (uh >> 2));
    }
};
} // namespace std

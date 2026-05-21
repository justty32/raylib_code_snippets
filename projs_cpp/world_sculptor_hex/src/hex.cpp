#include "mapcore/hex.hpp"
#include <cmath>

namespace mapcore {

const std::array<Hex, 6> DIRECTIONS = {{
    { 1,  0},   // 0: E
    { 1, -1},   // 1: NE
    { 0, -1},   // 2: NW
    {-1,  0},   // 3: W
    {-1,  1},   // 4: SW
    { 0,  1},   // 5: SE
}};

Hex Hex::neighbor(int direction_index) const noexcept {
    int d = ((direction_index % 6) + 6) % 6;
    return *this + DIRECTIONS[d];
}

std::array<Hex, 6> Hex::neighbors() const noexcept {
    return {{
        *this + DIRECTIONS[0],
        *this + DIRECTIONS[1],
        *this + DIRECTIONS[2],
        *this + DIRECTIONS[3],
        *this + DIRECTIONS[4],
        *this + DIRECTIONS[5],
    }};
}

int hex_distance(const Hex& a, const Hex& b) noexcept {
    int dq = a.q - b.q;
    int dr = a.r - b.r;
    return (std::abs(dq) + std::abs(dq + dr) + std::abs(dr)) / 2;
}

Hex hex_round(float qf, float rf) noexcept {
    float sf = -qf - rf;
    int rq = static_cast<int>(std::round(qf));
    int rr = static_cast<int>(std::round(rf));
    int rs = static_cast<int>(std::round(sf));

    float q_diff = std::abs(static_cast<float>(rq) - qf);
    float r_diff = std::abs(static_cast<float>(rr) - rf);
    float s_diff = std::abs(static_cast<float>(rs) - sf);

    if (q_diff > r_diff && q_diff > s_diff)
        rq = -rr - rs;
    else if (r_diff > s_diff)
        rr = -rq - rs;
    return {rq, rr};
}

std::vector<Hex> hex_line(const Hex& start, const Hex& end) {
    int n = hex_distance(start, end);
    if (n == 0) return {start};
    std::vector<Hex> results;
    results.reserve(static_cast<size_t>(n + 1));
    for (int i = 0; i <= n; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(n);
        float q = start.q * (1.0f - t) + end.q * t;
        float r = start.r * (1.0f - t) + end.r * t;
        results.push_back(hex_round(q, r));
    }
    return results;
}

std::vector<Hex> hex_ring(const Hex& center, int radius) {
    if (radius < 0) throw std::invalid_argument("radius must be >= 0");
    if (radius == 0) return {center};
    std::vector<Hex> results;
    results.reserve(static_cast<size_t>(6 * radius));
    Hex cube = center + DIRECTIONS[4] * radius;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < radius; ++j) {
            results.push_back(cube);
            cube = cube + DIRECTIONS[i];
        }
    }
    return results;
}

std::vector<Hex> hex_spiral(const Hex& center, int max_radius) {
    if (max_radius < 0) throw std::invalid_argument("max_radius must be >= 0");
    std::vector<Hex> results;
    // total = 1 + 3*N*(N+1)
    results.reserve(static_cast<size_t>(1 + 3 * max_radius * (max_radius + 1)));
    results.push_back(center);
    for (int radius = 1; radius <= max_radius; ++radius) {
        auto ring = hex_ring(center, radius);
        for (auto& h : ring) results.push_back(h);
    }
    return results;
}

} // namespace mapcore

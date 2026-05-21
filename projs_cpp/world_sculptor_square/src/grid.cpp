// 方格座標的幾何運算：鄰居、距離、連線、環與螺旋。
// 純座標計算，不依賴任何地圖資料；TileMap 與 generation 都建立在這層之上。
#include "mapcore/grid.hpp"
#include <cmath>

namespace mapcore {

// 4 方向位移表，索引固定為 0=E, 1=N, 2=W, 3=S。
// y 軸向下 (螢幕座標)，所以 N=(0,-1)、S=(0,+1)；對立方向 i ↔ (i+2)%4。
const std::array<Coord, 4> DIRECTIONS = {{
    { 1,  0},   // 0: E
    { 0, -1},   // 1: N
    {-1,  0},   // 2: W
    { 0,  1},   // 3: S
}};

// 取指定方向的鄰格。先用 ((d%4)+4)%4 將任意整數摺回 0..3，
// 因此 -1 等同 dir 3、4 等同 dir 0，呼叫端不必自行做邊界檢查。
Coord Coord::neighbor(int direction_index) const noexcept {
    int d = ((direction_index % 4) + 4) % 4;
    return *this + DIRECTIONS[d];
}

// 依固定順序 (E/N/W/S) 回傳 4 個鄰格；順序與 DIRECTIONS、rivers edge slot 一致。
std::array<Coord, 4> Coord::neighbors() const noexcept {
    return {{
        *this + DIRECTIONS[0],
        *this + DIRECTIONS[1],
        *this + DIRECTIONS[2],
        *this + DIRECTIONS[3],
    }};
}

// 方格曼哈頓距離 (4 連通下的最短步數)；對應 hex 版的 hex_distance。
int grid_distance(const Coord& a, const Coord& b) noexcept {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

// 兩格之間的直線連線。採用「4 連通」版的 Bresenham：每步只走水平或垂直
// 一格 (不走對角)，故輸出長度恰為 dx+dy+1，與曼哈頓距離一致。
std::vector<Coord> grid_line(const Coord& start, const Coord& end) {
    int dx = std::abs(end.x - start.x);
    int dy = std::abs(end.y - start.y);
    if (dx == 0 && dy == 0) return {start};
    int sx = (end.x > start.x) ? 1 : -1;   // x 前進方向
    int sy = (end.y > start.y) ? 1 : -1;   // y 前進方向
    std::vector<Coord> out;
    out.reserve(static_cast<size_t>(dx + dy + 1));
    int error = dx - dy;   // 誤差項：決定下一步該走 x 還是 y
    int x = start.x, y = start.y;
    out.push_back({x, y});
    for (int i = 0; i < dx + dy; ++i) {
        int e2 = 2 * error;
        if (e2 > -dy) { error -= dy; x += sx; }   // 誤差偏向 x，走一步水平
        else          { error += dx; y += sy; }   // 否則走一步垂直
        out.push_back({x, y});
    }
    return out;
}

// 以曼哈頓距離 == radius 的所有格子組成的「菱形環」(rhombus ring)。
// 從左頂點 (center.x-radius, center.y) 出發，沿菱形 4 條邊各走 radius 步，
// 共 4*radius 格。對應 hex 版的 hex_ring (六邊形環)。
std::vector<Coord> grid_ring(const Coord& center, int radius) {
    if (radius < 0) throw std::invalid_argument("radius must be >= 0");
    if (radius == 0) return {center};
    std::vector<Coord> out;
    out.reserve(static_cast<size_t>(4 * radius));
    int x = center.x - radius;   // 起點：菱形最左頂點
    int y = center.y;
    // 4 條邊的前進向量：右上 → 右下 → 左下 → 左上，繞行一圈
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

// 由內而外把 radius 0..max_radius 的環串接成螺旋序列 (含中心)。
// 常用於「就近搜尋」：愈早出現的格子離中心愈近。
std::vector<Coord> grid_spiral(const Coord& center, int max_radius) {
    if (max_radius < 0) throw std::invalid_argument("max_radius must be >= 0");
    std::vector<Coord> out;
    // 預留總格數：1 + 4*(1+2+...+max_radius) = 1 + 2*r*(r+1)
    out.reserve(static_cast<size_t>(1 + 2 * max_radius * (max_radius + 1)));
    out.push_back(center);
    for (int r = 1; r <= max_radius; ++r) {
        auto ring = grid_ring(center, r);
        for (auto& c : ring) out.push_back(c);
    }
    return out;
}

} // namespace mapcore

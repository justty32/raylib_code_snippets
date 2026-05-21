#include "mapcore/generation/depressions.hpp"
#include <queue>
#include <tuple>

namespace mapcore {
namespace generation {

// 4-連通偏移 (dx, dy)
static constexpr int GRID_NB[4][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}
};

DepressionResult fill_depressions(
    const std::vector<float>& heightmap,
    int width, int height,
    float sea_level)
{
    const int H = height, W = width;
    const int N = W * H;
    DepressionResult res;
    res.filled = heightmap;
    std::vector<bool> processed(N, false);

    using Entry = std::tuple<float, int, int>;  // (fill_h, y, x)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    auto push = [&](int y, int x, float h) {
        int idx = y * W + x;
        if (!processed[idx]) {
            processed[idx] = true;
            pq.push({h, y, x});
        }
    };

    // 邊界格 + 海洋格作為初始種子
    for (int x = 0; x < W; ++x) {
        push(0,   x, heightmap[x]);
        push(H-1, x, heightmap[(H-1)*W+x]);
    }
    for (int y = 1; y < H-1; ++y) {
        push(y, 0,   heightmap[y*W]);
        push(y, W-1, heightmap[y*W+W-1]);
    }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (heightmap[y*W+x] <= sea_level) push(y, x, heightmap[y*W+x]);

    while (!pq.empty()) {
        auto [fill_h, y, x] = pq.top(); pq.pop();
        res.filled[y*W+x] = fill_h;
        for (auto& nb : GRID_NB) {
            int nx = x + nb[0], ny = y + nb[1];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            int ni = ny*W+nx;
            if (processed[ni]) continue;
            float new_fill = (heightmap[ni] >= fill_h) ? heightmap[ni] : fill_h;
            processed[ni] = true;
            res.filled[ni] = new_fill;
            pq.push({new_fill, ny, nx});
        }
    }

    constexpr float EPS = 1e-9f;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (heightmap[y*W+x] > sea_level && res.filled[y*W+x] > heightmap[y*W+x] + EPS)
                res.lake_tiles.push_back({x, y});

    return res;
}

} // namespace generation
} // namespace mapcore

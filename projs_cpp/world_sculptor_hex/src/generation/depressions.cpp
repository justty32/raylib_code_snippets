#include "mapcore/generation/depressions.hpp"
#include <queue>
#include <tuple>

namespace mapcore {
namespace generation {

// 六向鄰居偏移 (dq, dr)
static constexpr int HEX_NB[6][2] = {
    {1,0},{-1,0},{0,1},{0,-1},{1,-1},{-1,1}
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

    using Entry = std::tuple<float, int, int>;  // (fill_h, r, q)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    auto push = [&](int r, int q, float h) {
        int idx = r * W + q;
        if (!processed[idx]) {
            processed[idx] = true;
            pq.push({h, r, q});
        }
    };

    // 邊界格 + 海洋格作為初始種子
    for (int q = 0; q < W; ++q) {
        push(0,   q, heightmap[q]);
        push(H-1, q, heightmap[(H-1)*W+q]);
    }
    for (int r = 1; r < H-1; ++r) {
        push(r, 0,   heightmap[r*W]);
        push(r, W-1, heightmap[r*W+W-1]);
    }
    for (int r = 0; r < H; ++r)
        for (int q = 0; q < W; ++q)
            if (heightmap[r*W+q] <= sea_level) push(r, q, heightmap[r*W+q]);

    while (!pq.empty()) {
        auto [fill_h, r, q] = pq.top(); pq.pop();
        res.filled[r*W+q] = fill_h;
        for (auto& nb : HEX_NB) {
            int nq = q + nb[0], nr = r + nb[1];
            if (nq < 0 || nq >= W || nr < 0 || nr >= H) continue;
            int ni = nr*W+nq;
            if (processed[ni]) continue;
            float new_fill = (heightmap[ni] >= fill_h) ? heightmap[ni] : fill_h;
            processed[ni] = true;
            res.filled[ni] = new_fill;
            pq.push({new_fill, nr, nq});
        }
    }

    constexpr float EPS = 1e-9f;
    for (int r = 0; r < H; ++r)
        for (int q = 0; q < W; ++q)
            if (heightmap[r*W+q] > sea_level && res.filled[r*W+q] > heightmap[r*W+q] + EPS)
                res.lake_tiles.push_back({r, q});

    return res;
}

} // namespace generation
} // namespace mapcore

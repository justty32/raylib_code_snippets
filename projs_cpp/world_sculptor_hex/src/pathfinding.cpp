#include "mapcore/pathfinding.hpp"
#include "mapcore/rivers.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

namespace mapcore {

float get_min_passable_cost() {
    static float cached = []() {
        float mn = std::numeric_limits<float>::infinity();
        for (auto* def : get_default_registry().all_defs()) {
            if (std::isfinite(def->move_cost))
                mn = std::min(mn, def->move_cost);
        }
        return mn;
    }();
    return cached;
}

// min-heap element: (f_score, counter, q, r)
using HeapEntry = std::tuple<float, int, int, int>;

static std::vector<Hex> reconstruct(
    const std::vector<int>& came_from_q,
    const std::vector<int>& came_from_r,
    int width, const Hex& end)
{
    std::vector<Hex> path;
    Hex cur = end;
    while (true) {
        path.push_back(cur);
        int idx = cur.r * width + cur.q;
        int pq = came_from_q[idx];
        int pr = came_from_r[idx];
        if (pq < 0) break;
        cur = {pq, pr};
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::optional<std::vector<Hex>> astar(
    const TileMap& tile_map,
    const Hex& start,
    const Hex& goal,
    float river_crossing_cost)
{
    if (!tile_map.in_bounds(start) || !tile_map.in_bounds(goal))
        return std::nullopt;
    if (start == goal) return std::vector<Hex>{start};

    const int W = tile_map.width();
    const int H = tile_map.height();
    const int N = W * H;
    const float INF = std::numeric_limits<float>::infinity();
    const float MIN_COST = get_min_passable_cost();

    std::vector<float> g_score(N, INF);
    std::vector<int>   came_from_q(N, -1);
    std::vector<int>   came_from_r(N, -1);
    std::vector<bool>  closed(N, false);

    g_score[start.r * W + start.q] = 0.0f;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> open;
    int counter = 0;
    open.push({static_cast<float>(hex_distance(start, goal)) * MIN_COST, counter++, start.q, start.r});

    while (!open.empty()) {
        auto [f, cnt, cq, cr] = open.top();
        open.pop();

        int cidx = cr * W + cq;
        if (closed[cidx]) continue;
        if (cq == goal.q && cr == goal.r)
            return reconstruct(came_from_q, came_from_r, W, goal);
        closed[cidx] = true;

        float cur_g = g_score[cidx];
        Hex current{cq, cr};
        for (int d = 0; d < 6; ++d) {
            Hex n = current + DIRECTIONS[d];
            if (!tile_map.in_bounds(n)) continue;
            const Tile* nt = tile_map.get(n);
            if (!is_passable(nt->terrain)) continue;
            int nidx = n.r * W + n.q;
            if (closed[nidx]) continue;

            float step = terrain_cost(nt->terrain);
            if (river_crossing_cost > 0.0f) {
                int rs = get_river_strength(tile_map, current, d);
                if (rs > 0) step += river_crossing_cost * rs;
            }
            float tentative = cur_g + step;
            if (tentative < g_score[nidx]) {
                g_score[nidx] = tentative;
                came_from_q[nidx] = cq;
                came_from_r[nidx] = cr;
                float fval = tentative + static_cast<float>(hex_distance(n, goal)) * MIN_COST;
                open.push({fval, counter++, n.q, n.r});
            }
        }
    }
    return std::nullopt;
}

float path_cost(const TileMap& tile_map, const std::vector<Hex>& path) {
    if (path.size() <= 1) return 0.0f;
    float total = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        const Tile* t = tile_map.get(path[i]);
        if (t) total += terrain_cost(t->terrain);
    }
    return total;
}

} // namespace mapcore

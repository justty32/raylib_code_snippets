#include "mapcore/generation/postprocess.hpp"
#include "mapcore/generation/classify.hpp"

namespace mapcore {
namespace generation {

std::vector<std::vector<Coord>> find_components(
    const TileMap& tile_map,
    std::function<bool(uint16_t)> predicate)
{
    const int W = tile_map.width(), H = tile_map.height();
    std::vector<bool> visited(W * H, false);
    std::vector<std::vector<Coord>> result;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = y * W + x;
            if (visited[idx]) continue;
            if (!predicate(tile_map.tile_at(x, y).terrain)) { visited[idx] = true; continue; }
            std::vector<Coord> comp;
            std::vector<Coord> stk = {{x, y}};
            visited[idx] = true;
            while (!stk.empty()) {
                Coord cur = stk.back(); stk.pop_back();
                comp.push_back(cur);
                for (const auto& d : DIRECTIONS) {
                    Coord nb = cur + d;
                    if (!tile_map.in_bounds(nb)) continue;
                    int ni = nb.y * W + nb.x;
                    if (visited[ni]) continue;
                    visited[ni] = true;
                    if (predicate(tile_map.tile_at(nb.x, nb.y).terrain)) stk.push_back(nb);
                }
            }
            result.push_back(std::move(comp));
        }
    }
    return result;
}

int remove_small_islands(TileMap& tile_map, int min_size) {
    if (min_size <= 1) return 0;
    auto is_land = [](uint16_t t) { return !get_default_registry().is_water(t); };
    int removed = 0;
    for (auto& comp : find_components(tile_map, is_land)) {
        if (static_cast<int>(comp.size()) < min_size) {
            for (const auto& c : comp) tile_map.set_terrain(c, TerrainType::OCEAN);
            removed += static_cast<int>(comp.size());
        }
    }
    return removed;
}

int remove_small_lakes(TileMap& tile_map, int max_size, uint16_t fill) {
    if (max_size < 1) return 0;
    const int W = tile_map.width(), H = tile_map.height();
    auto is_water = [](uint16_t t) { return get_default_registry().is_water(t); };
    int filled = 0;
    for (auto& comp : find_components(tile_map, is_water)) {
        if (static_cast<int>(comp.size()) > max_size) continue;
        bool on_edge = false;
        for (const auto& c : comp) {
            if (c.x == 0 || c.x == W-1 || c.y == 0 || c.y == H-1) { on_edge = true; break; }
        }
        if (on_edge) continue;
        for (const auto& c : comp) tile_map.set_terrain(c, fill);
        filled += static_cast<int>(comp.size());
    }
    return filled;
}

void relabel_coast(TileMap& tile_map, int coast_depth) {
    for (int y = 0; y < tile_map.height(); ++y)
        for (int x = 0; x < tile_map.width(); ++x)
            if (tile_map.tile_at(x, y).terrain == TerrainType::COAST)
                tile_map.tile_at(x, y).terrain = TerrainType::OCEAN;
    expand_coast(tile_map, coast_depth);
}

PostProcessResult post_process(
    TileMap& tile_map, int island_min_size, int lake_max_size,
    int coast_depth, uint16_t lake_fill)
{
    PostProcessResult res;
    res.islands_removed = remove_small_islands(tile_map, island_min_size);
    res.lakes_filled    = remove_small_lakes(tile_map, lake_max_size, lake_fill);
    relabel_coast(tile_map, coast_depth);
    return res;
}

} // namespace generation
} // namespace mapcore

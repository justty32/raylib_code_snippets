#include "mapcore/generation/postprocess.hpp"
#include "mapcore/generation/classify.hpp"

namespace mapcore {
namespace generation {

std::vector<std::vector<Hex>> find_components(
    const TileMap& tile_map,
    std::function<bool(uint16_t)> predicate)
{
    const int W = tile_map.width(), H = tile_map.height();
    std::vector<bool> visited(W * H, false);
    std::vector<std::vector<Hex>> result;

    for (int r = 0; r < H; ++r) {
        for (int q = 0; q < W; ++q) {
            int idx = r * W + q;
            if (visited[idx]) continue;
            if (!predicate(tile_map.tile_at(q, r).terrain)) { visited[idx] = true; continue; }
            std::vector<Hex> comp;
            std::vector<Hex> stk = {{q, r}};
            visited[idx] = true;
            while (!stk.empty()) {
                Hex h = stk.back(); stk.pop_back();
                comp.push_back(h);
                for (const auto& d : DIRECTIONS) {
                    Hex nb = h + d;
                    if (!tile_map.in_bounds(nb)) continue;
                    int ni = nb.r * W + nb.q;
                    if (visited[ni]) continue;
                    visited[ni] = true;
                    if (predicate(tile_map.tile_at(nb.q, nb.r).terrain)) stk.push_back(nb);
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
            for (const auto& h : comp) tile_map.set_terrain(h, TerrainType::OCEAN);
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
        for (const auto& h : comp) {
            if (h.q == 0 || h.q == W-1 || h.r == 0 || h.r == H-1) { on_edge = true; break; }
        }
        if (on_edge) continue;
        for (const auto& h : comp) tile_map.set_terrain(h, fill);
        filled += static_cast<int>(comp.size());
    }
    return filled;
}

void relabel_coast(TileMap& tile_map, int coast_depth) {
    for (int r = 0; r < tile_map.height(); ++r)
        for (int q = 0; q < tile_map.width(); ++q)
            if (tile_map.tile_at(q, r).terrain == TerrainType::COAST)
                tile_map.tile_at(q, r).terrain = TerrainType::OCEAN;
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

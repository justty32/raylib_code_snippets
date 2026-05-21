#include "mapcore/generation/classify.hpp"
#include <stdexcept>

namespace mapcore {
namespace generation {

TileMap heightmap_to_tilemap(
    const std::vector<float>& heightmap,
    int width, int height,
    float sea_level,
    int coast_depth)
{
    if (heightmap.size() != static_cast<size_t>(width * height))
        throw std::invalid_argument("heightmap size mismatch");
    if (sea_level < 0.0f || sea_level > 1.0f)
        throw std::invalid_argument("sea_level must be in [0, 1]");
    if (coast_depth < 0)
        throw std::invalid_argument("coast_depth must be >= 0");

    TileMap tile_map(width, height, TerrainType::PLAINS);
    for (int r = 0; r < height; ++r) {
        for (int q = 0; q < width; ++q) {
            if (heightmap[r * width + q] <= sea_level) {
                Tile* t = tile_map.get({q, r});
                t->terrain    = TerrainType::OCEAN;
                t->water_depth = sea_level - heightmap[r * width + q];
            }
        }
    }
    expand_coast(tile_map, coast_depth);
    return tile_map;
}

void expand_coast(TileMap& tile_map, int coast_depth) {
    if (coast_depth < 0)
        throw std::invalid_argument("coast_depth must be >= 0");
    for (int pass = 0; pass < coast_depth; ++pass) {
        std::vector<Hex> to_coast;
        for (int r = 0; r < tile_map.height(); ++r) {
            for (int q = 0; q < tile_map.width(); ++q) {
                if (tile_map.tile_at(q, r).terrain != TerrainType::OCEAN) continue;
                Hex h{q, r};
                bool has_non_ocean = false;
                for (const auto& nb : tile_map.neighbors(h)) {
                    if (tile_map.tile_at(nb.q, nb.r).terrain != TerrainType::OCEAN) {
                        has_non_ocean = true; break;
                    }
                }
                if (has_non_ocean) to_coast.push_back(h);
            }
        }
        if (to_coast.empty()) return;
        for (const auto& h : to_coast)
            tile_map.set_terrain(h, TerrainType::COAST);
    }
}

} // namespace generation
} // namespace mapcore

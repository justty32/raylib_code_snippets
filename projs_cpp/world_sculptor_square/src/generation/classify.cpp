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
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (heightmap[y * width + x] <= sea_level) {
                Tile* t = tile_map.get({x, y});
                t->terrain     = TerrainType::OCEAN;
                t->water_depth = sea_level - heightmap[y * width + x];
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
        std::vector<Coord> to_coast;
        for (int y = 0; y < tile_map.height(); ++y) {
            for (int x = 0; x < tile_map.width(); ++x) {
                if (tile_map.tile_at(x, y).terrain != TerrainType::OCEAN) continue;
                Coord c{x, y};
                bool has_non_ocean = false;
                for (const auto& nb : tile_map.neighbors(c)) {
                    if (tile_map.tile_at(nb.x, nb.y).terrain != TerrainType::OCEAN) {
                        has_non_ocean = true; break;
                    }
                }
                if (has_non_ocean) to_coast.push_back(c);
            }
        }
        if (to_coast.empty()) return;
        for (const auto& c : to_coast)
            tile_map.set_terrain(c, TerrainType::COAST);
    }
}

} // namespace generation
} // namespace mapcore

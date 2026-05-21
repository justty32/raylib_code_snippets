#include "mapcore/generation/biome.hpp"
#include <algorithm>
#include <cmath>

namespace mapcore {
namespace generation {

void apply_biomes(
    TileMap& tile_map,
    const std::vector<float>& heightmap,
    const std::vector<float>& moisture,
    float sea_level,
    const BiomeParams& p)
{
    const int W = tile_map.width(), H = tile_map.height();
    const float half = std::max((H - 1) / 2.0f, 1e-9f);
    const float span = std::max(1.0f - sea_level, 1e-9f);

    for (int r = 0; r < H; ++r) {
        for (int q = 0; q < W; ++q) {
            Tile& tile = tile_map.tile_at(q, r);
            uint16_t t = tile.terrain;
            if (t == TerrainType::OCEAN || t == TerrainType::COAST || t == TerrainType::LAKE)
                continue;

            int idx = r * W + q;
            float elev = heightmap[idx];

            if (elev > p.mountain_threshold) { tile.terrain = TerrainType::MOUNTAIN; continue; }
            if (elev > p.hill_threshold)     { tile.terrain = TerrainType::HILL;     continue; }

            float latitude = std::abs(r - (H - 1) / 2.0f) / half;
            float elev_above_sea = std::max(0.0f, elev - sea_level) / span;
            float temp = std::clamp(1.0f - latitude - p.elevation_temp_factor * elev_above_sea, 0.0f, 1.0f);
            float moist = moisture[idx];

            if (temp < p.snow_temp) {
                tile.terrain = TerrainType::SNOW;
            } else if (temp < p.tundra_temp) {
                tile.terrain = TerrainType::TUNDRA;
            } else if (temp >= p.hot_temp) {
                if (moist < p.dry_moisture)       tile.terrain = TerrainType::DESERT;
                else if (moist > p.wet_moisture)  tile.terrain = TerrainType::FOREST;
                else                              tile.terrain = TerrainType::PLAINS;
            } else {
                if (moist < p.dry_moisture)       tile.terrain = TerrainType::PLAINS;
                else if (moist > p.wet_moisture)  tile.terrain = TerrainType::FOREST;
                else                              tile.terrain = TerrainType::GRASSLAND;
            }
        }
    }
}

} // namespace generation
} // namespace mapcore

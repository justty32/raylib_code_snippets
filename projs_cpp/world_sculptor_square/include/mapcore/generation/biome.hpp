#pragma once
// 生物群系分類 (Phase 3)。
// 把 TileMap 上的陸地細分為 MOUNTAIN / HILL / SNOW / TUNDRA / DESERT / FOREST / GRASSLAND / PLAINS。

#include "mapcore/map.hpp"
#include <vector>

namespace mapcore {
namespace generation {

struct BiomeParams {
    float mountain_threshold  = 0.85f;
    float hill_threshold      = 0.70f;
    float snow_temp           = 0.15f;
    float tundra_temp         = 0.30f;
    float hot_temp            = 0.65f;
    float dry_moisture        = 0.30f;
    float wet_moisture        = 0.65f;
    float elevation_temp_factor = 0.5f;
};

// In-place 細分陸地；OCEAN/COAST/LAKE 不動。
// heightmap, moisture: flat row-major, size = W*H。
void apply_biomes(
    TileMap& tile_map,
    const std::vector<float>& heightmap,
    const std::vector<float>& moisture,
    float sea_level = 0.4f,
    const BiomeParams& params = {}
);

} // namespace generation
} // namespace mapcore

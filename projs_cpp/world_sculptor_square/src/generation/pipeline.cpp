#include "mapcore/generation/pipeline.hpp"
#include "mapcore/generation/classify.hpp"
#include "mapcore/generation/postprocess.hpp"
#include "mapcore/generation/depressions.hpp"
#include "mapcore/rivers.hpp"
#include <algorithm>

namespace mapcore {
namespace generation {

WorldGenResult generate_world(
    int width, int height,
    std::optional<uint64_t> seed,
    const WorldGenParams& p)
{
    TerrainRegistry* reg = p.registry ? p.registry : &get_default_registry();

    // ── Phase 1：heightmap ──────────────────────────────────────────────────
    HeightmapParams hm_params = p.heightmap_params;
    // 套用 base noise 參數（pipeline 層覆蓋 heightmap_params 的預設值）
    hm_params.octaves        = p.octaves;
    hm_params.persistence    = p.persistence;
    hm_params.base_frequency = p.base_frequency;
    hm_params.shape_sea_level = p.sea_level;

    // 形狀自動 noise 頻率（對齊 Python SHAPE_NOISE_DEFAULTS）
    struct ShapeNoiseDef { const char* shape; int octaves; int freq; };
    static const ShapeNoiseDef SHAPE_DEFAULTS[] = {
        {"pangaea",               4, 3},
        {"continents",            4, 4},
        {"ring_sea",              4, 4},
        {"island",                4, 4},
        {"archipelago",           5, 5},
        {"shattered_archipelago", 6, 7},
    };
    if (!hm_params.shape.empty()) {
        for (auto& sd : SHAPE_DEFAULTS) {
            if (hm_params.shape == sd.shape) {
                hm_params.octaves        = sd.octaves;
                hm_params.base_frequency = sd.freq;
                break;
            }
        }
    }

    auto heightmap = generate_heightmap(width, height, seed, hm_params);

    // ── Phase 1b：moisture（獨立 seed）──────────────────────────────────────
    HeightmapParams moist_params;
    moist_params.octaves        = p.octaves;
    moist_params.persistence    = p.persistence;
    moist_params.base_frequency = p.base_frequency;
    std::optional<uint64_t> moist_seed;
    if (seed) moist_seed = *seed + MOISTURE_SEED_OFFSET;
    auto moisture = generate_heightmap(width, height, moist_seed, moist_params);

    // ── Phase 2：classify ───────────────────────────────────────────────────
    auto tile_map = heightmap_to_tilemap(heightmap, width, height, p.sea_level, p.coast_depth);

    // ── Phase 3：biome ──────────────────────────────────────────────────────
    apply_biomes(tile_map, heightmap, moisture, p.sea_level, p.biome_params);

    // ── Phase 4：post-process ────────────────────────────────────────────────
    if (p.post_process)
        post_process(tile_map, p.island_min_size, p.lake_max_size, p.coast_depth, p.lake_fill);

    // ── Phase 4.5：窪地填充 → LAKE ─────────────────────────────────────────
    if (p.lake_depressions) {
        auto dr = fill_depressions(heightmap, width, height, p.sea_level);
        for (const auto& c : dr.lake_tiles) {
            Tile& tile = tile_map.tile_at(c.x, c.y);
            tile.terrain     = TerrainType::LAKE;
            tile.water_depth = dr.filled[c.y * width + c.x] - heightmap[c.y * width + c.x];
        }
    }

    // ── Phase 5：climate ─────────────────────────────────────────────────────
    ClimateResult climate_result;
    if (p.climate) {
        std::optional<uint64_t> clim_seed;
        if (seed) clim_seed = *seed + CLIMATE_SEED_OFFSET;
        climate_result = apply_climate(tile_map, heightmap, moisture, clim_seed, p.climate_params);
    }

    // ── Phase 5b：rivers ─────────────────────────────────────────────────────
    if (p.rivers) {
        std::optional<uint64_t> riv_seed;
        if (seed) riv_seed = *seed + RIVERS_SEED_OFFSET;
        const auto* rainfall_ptr  = climate_result.rainfall_mm.empty()
            ? &moisture : &climate_result.rainfall_mm;
        const auto* temp_ptr = climate_result.temperature_celsius.empty()
            ? nullptr : &climate_result.temperature_celsius;
        generate_rivers(tile_map, heightmap, *rainfall_ptr, temp_ptr, riv_seed, p.river_params);
    }

    // ── Phase 6：features ────────────────────────────────────────────────────
    if (p.features)
        tile_map.features = apply_features(tile_map, p.feature_workers);

    // ── extra_noise ──────────────────────────────────────────────────────────
    std::unordered_map<std::string, std::vector<float>> extra_noise;
    for (auto& [name, offset] : p.extra_noise_specs) {
        std::optional<uint64_t> n_seed;
        if (seed) n_seed = *seed + EXTRA_NOISE_BASE_OFFSET + static_cast<uint64_t>(offset);
        HeightmapParams en_params;
        en_params.octaves        = p.octaves;
        en_params.persistence    = p.persistence;
        en_params.base_frequency = p.base_frequency;
        extra_noise[name] = generate_heightmap(width, height, n_seed, en_params);
    }

    WorldGenResult result{
        std::move(tile_map),
        std::move(heightmap),
        std::move(moisture),
        std::move(climate_result.temperature_celsius),
        std::move(climate_result.rainfall_mm),
        std::move(extra_noise),
        reg,
        seed
    };
    return result;
}

} // namespace generation
} // namespace mapcore

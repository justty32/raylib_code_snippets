#include "mapcore/generation/climate.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace mapcore {
namespace generation {

// 對齊 Python _piecewise_linear
static float piecewise_linear(
    const float* xs, const float* ys, int n, float x) noexcept
{
    if (x <= xs[0]) return ys[0];
    if (x >= xs[n-1]) return ys[n-1];
    for (int i = 0; i < n-1; ++i) {
        if (x <= xs[i+1]) {
            if (xs[i+1] == xs[i]) return ys[i+1];
            float t = (x - xs[i]) / (xs[i+1] - xs[i]);
            return ys[i] + t * (ys[i+1] - ys[i]);
        }
    }
    return ys[n-1];
}

static const float AVG_TEMP_X[] = {0.0f, 0.1f, 0.5f, 1.0f};
static const float AVG_TEMP_Y[] = {30.0f, 29.0f, 7.0f, -37.0f};
static const float RAIN_LAT_X[] = {0.0f, 25.0f, 45.0f, 70.0f, 80.0f, 90.0f};
static const float RAIN_LAT_Y[] = {1.12f, 0.94f, 0.70f, 0.30f, 0.05f, 0.05f};

float latitude_normalized(int r, int total_h) noexcept {
    float half = std::max((total_h - 1) / 2.0f, 1e-9f);
    return std::abs(r - (total_h - 1) / 2.0f) / half;
}

float base_temperature_celsius(float lat_norm) noexcept {
    return piecewise_linear(AVG_TEMP_X, AVG_TEMP_Y, 4, lat_norm);
}

static float temperature_reduction_at_elevation(float elev,
    float start = 0.05f, float end = 1.0f, float max_red = 40.0f) noexcept
{
    if (elev < start) return 0.0f;
    if (end <= start) return max_red;
    float t = std::min(1.0f, (elev - start) / (end - start));
    return max_red * t;
}

float compute_temperature_celsius(int r, int total_h, float elev, float noise_offset) noexcept {
    float lat = latitude_normalized(r, total_h);
    return base_temperature_celsius(lat) - temperature_reduction_at_elevation(elev) + noise_offset;
}

static float rainfall_lat_mod(float lat_norm) noexcept {
    return piecewise_linear(RAIN_LAT_X, RAIN_LAT_Y, 6, lat_norm * 90.0f);
}

static float rainfall_squash(float val) noexcept {
    if (val < 0.0f) val = 0.0f;
    if (val < 0.12f) {
        val = (val + 0.12f) / 2.0f;
        if (val < 0.03f) val = (val + 0.03f) / 2.0f;
    }
    return val;
}

float compute_rainfall_mm(int r, int total_h, float elev, float base_noise) noexcept {
    float lat = latitude_normalized(r, total_h);
    float val = base_noise * rainfall_lat_mod(lat);
    constexpr float elev_dry_start = 0.1f, elev_dry_end = 1.0f;
    if (elev > elev_dry_start) {
        float t = std::min(1.0f, (elev - elev_dry_start) / (elev_dry_end - elev_dry_start));
        val *= std::max(0.0f, 1.0f - t);
    }
    val = rainfall_squash(val);
    val = std::max(0.0f, std::pow(val, 1.5f));
    val = std::min(val, 0.999f);
    return val * 4000.0f;
}

Hilliness compute_hilliness(
    float elev, float sea_level,
    float hill_threshold, float mountain_threshold,
    float impassable_threshold,
    float random01) noexcept
{
    if (elev <= sea_level) return Hilliness::FLAT;
    if (elev < hill_threshold) {
        if (random01 >= 0.0f && random01 < 0.15f) return Hilliness::SMALL_HILLS;
        return Hilliness::FLAT;
    }
    if (elev < mountain_threshold) {
        if (random01 >= 0.0f && random01 < 0.5f) return Hilliness::LARGE_HILLS;
        return Hilliness::SMALL_HILLS;
    }
    if (elev < impassable_threshold) return Hilliness::MOUNTAINOUS;
    return Hilliness::IMPASSABLE;
}

static void apply_rain_shadow(
    std::vector<float>& rainfall_mm,
    const std::vector<float>& heightmap,
    int W, int H,
    float hill_threshold, float strength,
    float shadow_decay = 0.88f)
{
    for (int r = 0; r < H; ++r) {
        float shadow = 0.0f;
        for (int q = 0; q < W; ++q) {
            float elev = heightmap[r * W + q];
            float& rain = rainfall_mm[r * W + q];
            if (elev > hill_threshold) {
                float barrier = (elev - hill_threshold) / std::max(1.0f - hill_threshold, 1e-9f);
                rain = std::min(4000.0f, rain * (1.0f + 0.4f * barrier * strength));
                shadow = std::min(shadow + barrier * strength, 2.5f);
            } else if (shadow > 0.005f) {
                float factor = std::max(0.2f, 1.0f - shadow * 0.45f * strength);
                rain = std::max(0.0f, rain * factor);
            }
            shadow *= shadow_decay;
        }
    }
}

ClimateResult apply_climate(
    TileMap& tile_map,
    const std::vector<float>& heightmap,
    const std::vector<float>& rainfall_noise,
    std::optional<uint64_t> seed,
    const ClimateParams& params)
{
    const int W = tile_map.width(), H = tile_map.height();
    const int N = W * H;
    std::mt19937 rng(seed.has_value() ? static_cast<uint32_t>(*seed) : std::random_device{}());
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    ClimateResult result;
    result.temperature_celsius.resize(N);
    result.rainfall_mm.resize(N);

    for (int r = 0; r < H; ++r) {
        for (int q = 0; q < W; ++q) {
            int idx = r * W + q;
            float elev = heightmap[idx];
            float offset = (dist01(rng) * 2.0f - 1.0f) * params.temperature_offset_amp;
            result.temperature_celsius[idx] = compute_temperature_celsius(r, H, elev, offset);
            result.rainfall_mm[idx]         = compute_rainfall_mm(r, H, elev, rainfall_noise[idx]);

            Tile& tile = tile_map.tile_at(q, r);
            if (tile.terrain == TerrainType::OCEAN || tile.terrain == TerrainType::COAST) {
                tile.hilliness = Hilliness::FLAT;
            } else {
                tile.hilliness = compute_hilliness(
                    elev, params.sea_level,
                    params.hill_threshold, params.mountain_threshold,
                    params.impassable_threshold,
                    dist01(rng));
            }
        }
    }
    if (params.rain_shadow_strength > 0.0f) {
        apply_rain_shadow(result.rainfall_mm, heightmap, W, H,
                          params.hill_threshold, params.rain_shadow_strength);
    }
    return result;
}

} // namespace generation
} // namespace mapcore

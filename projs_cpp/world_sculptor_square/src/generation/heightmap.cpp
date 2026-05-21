#include "mapcore/generation/heightmap.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace mapcore {
namespace generation {

constexpr float RIDGE_ANISOTROPY = 0.1f;
constexpr uint64_t PLATE_SEED_XOR = 0x517CC1B7ULL;

static float smoothstep(float t) noexcept {
    return t * t * (3.0f - 2.0f * t);
}

static float bilinear(const std::vector<float>& coarse, int gw, int gh, float x, float y) noexcept {
    x = std::max(0.0f, std::min(x, static_cast<float>(gw - 1)));
    y = std::max(0.0f, std::min(y, static_cast<float>(gh - 1)));
    int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    int x1 = std::min(x0 + 1, gw - 1), y1 = std::min(y0 + 1, gh - 1);
    float fx = smoothstep(x - x0), fy = smoothstep(y - y0);
    float top = coarse[y0 * gw + x0] * (1.0f - fx) + coarse[y0 * gw + x1] * fx;
    float bot = coarse[y1 * gw + x0] * (1.0f - fx) + coarse[y1 * gw + x1] * fx;
    return top * (1.0f - fy) + bot * fy;
}

// 板塊邊界場：boundary_strength, cos_grid, sin_grid（各 W*H）
struct PlateField {
    std::vector<float> boundary_strength;
    std::vector<float> cos_grid;
    std::vector<float> sin_grid;
};

static PlateField make_plate_field(
    int width, int height, int num_plates, float boundary_width_pixels,
    std::mt19937& rng)
{
    if (num_plates < 2) throw std::invalid_argument("num_plates must be >= 2");
    float margin = std::max(std::min(width, height) * 0.02f, 1.0f);
    std::uniform_real_distribution<float> distW(margin, std::max(width  - 1 - margin, margin + 1.0f));
    std::uniform_real_distribution<float> distH(margin, std::max(height - 1 - margin, margin + 1.0f));
    std::vector<float> sx(num_plates), sy(num_plates);
    for (int i = 0; i < num_plates; ++i) { sx[i] = distW(rng); sy[i] = distH(rng); }

    const int N = width * height;
    PlateField pf;
    pf.boundary_strength.assign(N, 0.0f);
    pf.cos_grid.assign(N, 1.0f);
    pf.sin_grid.assign(N, 0.0f);
    float inv_bw = boundary_width_pixels > 0.0f ? 1.0f / boundary_width_pixels : 0.0f;

    for (int r = 0; r < height; ++r) {
        for (int q = 0; q < width; ++q) {
            float d1_sq = 1e18f, d2_sq = 1e18f;
            int i1 = 0, i2 = 0;
            for (int i = 0; i < num_plates; ++i) {
                float d = (q - sx[i]) * (q - sx[i]) + (r - sy[i]) * (r - sy[i]);
                if (d < d1_sq) { d2_sq = d1_sq; i2 = i1; d1_sq = d; i1 = i; }
                else if (d < d2_sq) { d2_sq = d; i2 = i; }
            }
            float d1 = std::sqrt(d1_sq), d2 = std::sqrt(d2_sq);
            float bd = (d2 - d1) * 0.5f;
            int idx = r * width + q;
            if (inv_bw > 0.0f) {
                float t = 1.0f - bd * inv_bw;
                if (t > 0.0f) pf.boundary_strength[idx] = t * t * (3.0f - 2.0f * t);
            }
            float vx = sx[i2] - sx[i1], vy = sy[i2] - sy[i1];
            float vlen = std::sqrt(vx*vx + vy*vy);
            if (vlen < 1e-9f) { pf.cos_grid[idx] = 1.0f; pf.sin_grid[idx] = 0.0f; }
            else { pf.cos_grid[idx] = -vy / vlen; pf.sin_grid[idx] = vx / vlen; }
        }
    }
    return pf;
}

static std::vector<float> make_shape_mask(
    int width, int height, const std::string& shape,
    std::mt19937& rng,
    const std::unordered_map<std::string, float>& params,
    float sea_level)
{
    auto get_p = [&](const std::string& key, float def) {
        auto it = params.find(key); return it != params.end() ? it->second : def;
    };
    auto get_pi = [&](const std::string& key, int def) {
        auto it = params.find(key); return it != params.end() ? static_cast<int>(it->second) : def;
    };
    const int N = width * height;
    std::vector<float> mask(N, 0.0f);
    (void)sea_level;

    if (shape == "island") {
        float cx = (width - 1) / 2.0f, cy = (height - 1) / 2.0f;
        float rx = std::max(cx, 1.0f), ry = std::max(cy, 1.0f);
        for (int r = 0; r < height; ++r)
            for (int q = 0; q < width; ++q) {
                float dx = (q - cx) / rx, dy = (r - cy) / ry;
                float d = std::sqrt(dx*dx + dy*dy);
                mask[r*width+q] = std::max(0.0f, 1.0f - std::pow(d, 1.4f));
            }
    } else if (shape == "archipelago") {
        int n = 3 + static_cast<int>(rng() % 4);
        float radius = std::min(width, height) * 0.22f;
        std::uniform_real_distribution<float> cx_d(0.15f * width,  0.85f * width);
        std::uniform_real_distribution<float> cy_d(0.15f * height, 0.85f * height);
        std::vector<float> icx(n), icy(n);
        for (int i = 0; i < n; ++i) { icx[i] = cx_d(rng); icy[i] = cy_d(rng); }
        for (int r = 0; r < height; ++r)
            for (int q = 0; q < width; ++q) {
                float best = 0.0f;
                for (int i = 0; i < n; ++i) {
                    float dx = (q - icx[i]) / radius, dy = (r - icy[i]) / (radius * 0.8f);
                    float d = std::sqrt(dx*dx + dy*dy);
                    best = std::max(best, std::max(0.0f, 1.0f - std::pow(d, 1.4f)));
                }
                mask[r*width+q] = best;
            }
    } else if (shape == "pangaea") {
        float land_ratio = get_p("land_ratio", 0.55f);
        float cx = (width - 1) / 2.0f, cy = (height - 1) / 2.0f;
        float half_diag = std::sqrt(cx*cx + cy*cy);
        float land_r = std::max(half_diag * land_ratio, 1.0f);
        for (int r = 0; r < height; ++r)
            for (int q = 0; q < width; ++q) {
                float d = std::sqrt((q-cx)*(q-cx) + (r-cy)*(r-cy));
                mask[r*width+q] = std::max(0.0f, 1.0f - std::pow(d / land_r, 1.3f));
            }
    } else if (shape == "continents") {
        int num_c = get_pi("num_continents", 3);
        float land_ratio = get_p("land_ratio", 0.4f);
        float min_side = static_cast<float>(std::min(width, height));
        float blob_r = std::max(min_side * 0.12f,
            std::sqrt(land_ratio * width * height / num_c) * 0.65f);
        float min_spacing = blob_r * 1.2f;
        float margin = blob_r * 0.4f;
        std::uniform_real_distribution<float> cxd(margin, width  - 1 - margin);
        std::uniform_real_distribution<float> cyd(margin, height - 1 - margin);
        std::uniform_real_distribution<float> ard(0.6f, 1.5f);
        struct C { float x, y, ar; };
        std::vector<C> centers;
        for (int attempt = 0; attempt < num_c * 40 && static_cast<int>(centers.size()) < num_c; ++attempt) {
            float cx = cxd(rng), cy = cyd(rng);
            bool ok = true;
            for (auto& c : centers) {
                float d2 = (cx-c.x)*(cx-c.x) + (cy-c.y)*(cy-c.y);
                if (d2 < min_spacing*min_spacing) { ok = false; break; }
            }
            if (ok) centers.push_back({cx, cy, ard(rng)});
        }
        while (static_cast<int>(centers.size()) < num_c)
            centers.push_back({cxd(rng), cyd(rng), 1.0f});
        for (int r = 0; r < height; ++r)
            for (int q = 0; q < width; ++q) {
                float best = 0.0f;
                for (auto& c : centers) {
                    float dx = (q - c.x) / blob_r, dy = (r - c.y) / (blob_r * c.ar);
                    float d = std::sqrt(dx*dx + dy*dy);
                    best = std::max(best, std::max(0.0f, 1.0f - std::pow(d, 1.2f)));
                }
                mask[r*width+q] = best;
            }
    } else if (shape == "ring_sea") {
        float land_ratio = get_p("land_ratio", 0.4f);
        float cx = (width-1)/2.0f, cy = (height-1)/2.0f;
        float min_half = std::min(cx, cy);
        float ring_r = min_half * 0.55f;
        float ring_w = std::max(min_half * (0.2f + land_ratio * 0.35f), 1.0f);
        for (int r = 0; r < height; ++r)
            for (int q = 0; q < width; ++q) {
                float d = std::sqrt((q-cx)*(q-cx) + (r-cy)*(r-cy));
                float dfr = std::abs(d - ring_r);
                mask[r*width+q] = std::max(0.0f, 1.0f - std::pow(dfr / ring_w, 1.5f));
            }
    } else if (shape == "shattered_archipelago") {
        int num_islands = get_pi("num_islands", 12);
        float island_size = get_p("island_size", 0.08f);
        float radius = std::max(std::min(width, height) * island_size, 2.0f);
        std::uniform_real_distribution<float> cxd(0.05f*width,  0.95f*width);
        std::uniform_real_distribution<float> cyd(0.05f*height, 0.95f*height);
        std::uniform_real_distribution<float> ard(0.7f, 1.4f);
        struct C { float x, y, ar; };
        std::vector<C> centers(num_islands);
        for (auto& c : centers) c = {cxd(rng), cyd(rng), ard(rng)};
        for (int r = 0; r < height; ++r)
            for (int q = 0; q < width; ++q) {
                float best = 0.0f;
                for (auto& c : centers) {
                    float dx = (q - c.x) / radius, dy = (r - c.y) / (radius * c.ar);
                    float d = std::sqrt(dx*dx + dy*dy);
                    best = std::max(best, std::max(0.0f, 1.0f - std::pow(d, 1.5f)));
                }
                mask[r*width+q] = best;
            }
    } else {
        throw std::invalid_argument("unknown shape: " + shape);
    }
    return mask;
}

std::vector<float> generate_heightmap(
    int width, int height,
    std::optional<uint64_t> seed,
    const HeightmapParams& p)
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("width and height must be > 0");
    if (p.octaves <= 0)
        throw std::invalid_argument("octaves must be > 0");
    if (p.persistence <= 0.0f || p.persistence > 1.0f)
        throw std::invalid_argument("persistence must be in (0, 1]");
    if (p.base_frequency < 1)
        throw std::invalid_argument("base_frequency must be >= 1");
    if (p.ridge_weight < 0.0f || p.ridge_weight > 1.0f)
        throw std::invalid_argument("ridge_weight must be in [0, 1]");

    const int N = width * height;
    std::mt19937 rng(seed.has_value() ? static_cast<uint32_t>(*seed) : std::random_device{}());
    std::uniform_real_distribution<float> rand01(0.0f, 1.0f);

    std::vector<float> grid(N, 0.0f);
    float total_weight = 0.0f;
    bool use_ridge = p.ridge_weight > 0.0f;

    // plates or global cos/sin grids
    std::vector<float> cos_grid, sin_grid, gate;
    float cos_a = 1.0f, sin_a = 0.0f;
    bool use_grid = false;

    if (use_ridge) {
        if (p.ridge_mode == "plates") {
            std::mt19937 plate_rng(seed.has_value()
                ? static_cast<uint32_t>(*seed ^ PLATE_SEED_XOR)
                : std::random_device{}());
            float bw_px = p.plate_boundary_width * std::min(width, height);
            auto pf = make_plate_field(width, height, p.num_plates, bw_px, plate_rng);
            gate     = std::move(pf.boundary_strength);
            cos_grid = std::move(pf.cos_grid);
            sin_grid = std::move(pf.sin_grid);
            use_grid = true;
        } else { // global
            if (p.ridge_direction_variation > 0.0f) {
                int dir_freq = std::max(2, p.base_frequency / 2);
                int dir_gw = dir_freq + 1, dir_gh = dir_freq + 1;
                std::mt19937 dir_rng(seed.has_value()
                    ? static_cast<uint32_t>(*seed ^ 0x9E3779B9ULL)
                    : std::random_device{}());
                std::vector<float> dir_coarse(dir_gw * dir_gh);
                for (auto& v : dir_coarse) v = rand01(dir_rng);
                float dxs = static_cast<float>(dir_freq) / std::max(width  - 1, 1);
                float dys = static_cast<float>(dir_freq) / std::max(height - 1, 1);
                cos_grid.resize(N); sin_grid.resize(N);
                for (int r = 0; r < height; ++r)
                    for (int q = 0; q < width; ++q) {
                        float dn = bilinear(dir_coarse, dir_gw, dir_gh, q * dxs, r * dys);
                        float local_dir = p.ridge_direction + (dn - 0.5f) * p.ridge_direction_variation;
                        float a = (90.0f - local_dir) * (3.14159265f / 180.0f);
                        cos_grid[r*width+q] = std::cos(a);
                        sin_grid[r*width+q] = std::sin(a);
                    }
                use_grid = true;
            } else {
                float rad = (90.0f - p.ridge_direction) * (3.14159265f / 180.0f);
                cos_a = std::cos(rad); sin_a = std::sin(rad);
            }
        }
    }

    // Musgrave 多分形 carry
    std::vector<float> ridge_carry;
    bool use_multifractal = use_ridge && p.ridge_multifractal_gain > 0.0f;
    if (use_multifractal) ridge_carry.assign(N, 1.0f);

    for (int octave = 0; octave < p.octaves; ++octave) {
        int freq = p.base_frequency * (1 << octave);
        float weight = std::pow(p.persistence, static_cast<float>(octave));
        total_weight += weight;
        int gw = freq + 1, gh = freq + 1;
        std::vector<float> coarse(gw * gh);
        for (auto& v : coarse) v = rand01(rng);
        float xs = static_cast<float>(freq) / std::max(width  - 1, 1);
        float ys = static_cast<float>(freq) / std::max(height - 1, 1);
        float hx = freq * 0.5f, hy = freq * 0.5f;
        for (int r = 0; r < height; ++r) {
            float cy = r * ys;
            for (int q = 0; q < width; ++q) {
                float cx = q * xs;
                float raw = bilinear(coarse, gw, gh, cx, cy);
                int idx = r * width + q;
                if (use_ridge) {
                    float local_w = p.ridge_weight;
                    if (!gate.empty()) local_w *= gate[idx];
                    if (local_w > 0.0f) {
                        float ca, sa;
                        if (use_grid) { ca = cos_grid[idx]; sa = sin_grid[idx]; }
                        else { ca = cos_a; sa = sin_a; }
                        float dx = cx - hx, dy = cy - hy;
                        float rx = dx * ca + dy * sa;
                        float ry = -dx * sa + dy * ca;
                        float raw_dir = bilinear(coarse, gw, gh, rx * RIDGE_ANISOTROPY + hx, ry + hy);
                        float fold = 1.0f - std::abs(2.0f * raw_dir - 1.0f);
                        if (p.ridge_power != 1.0f) fold = std::pow(fold, p.ridge_power);
                        if (use_multifractal) {
                            fold *= ridge_carry[idx];
                            ridge_carry[idx] = std::min(1.0f, fold * p.ridge_multifractal_gain);
                        }
                        raw = raw_dir * (1.0f - local_w) + fold * local_w;
                    }
                }
                grid[idx] += weight * raw;
            }
        }
    }

    float inv = 1.0f / total_weight;
    for (auto& v : grid) v *= inv;

    // 形狀遮罩
    if (!p.shape.empty()) {
        std::mt19937 shape_rng(seed.has_value()
            ? static_cast<uint32_t>(*seed + 0x1F2E3D4CULL)
            : std::random_device{}());
        auto mask = make_shape_mask(width, height, p.shape, shape_rng, p.shape_params, p.shape_sea_level);
        float s = p.shape_strength, sl = p.shape_sea_level;
        for (int i = 0; i < N; ++i) {
            float m = mask[i];
            float target = m * (sl + grid[i] * (1.0f - sl));
            grid[i] = grid[i] * (1.0f - s) + target * s;
        }
    }
    return grid;
}

} // namespace generation
} // namespace mapcore

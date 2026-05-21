#include "tools.hpp"

#include "grid_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace viewer {

int ToolRng::uniform_int(int lo, int hi) {
    if (hi < lo) std::swap(lo, hi);
    std::uniform_int_distribution<int> d(lo, hi);
    return d(engine_);
}

float ToolRng::uniform_float(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    std::uniform_real_distribution<float> d(lo, hi);
    return d(engine_);
}

// ── 高斯筆刷 ──────────────────────────────────────────────────────────

namespace {

[[nodiscard]] float gaussian(float dist, float radius) noexcept {
    if (radius <= 0.0f) return 1.0f;
    const float sigma = radius / 2.2f;
    const float n = dist / sigma;
    return std::exp(-0.5f * n * n);
}

} // anonymous

void apply_brush(EditorState& s, int x, int y, float delta) {
    const int radius = std::max(1, s.brush_size);
    // 走方形 disk，但用 Euclidean 距離當衰減：圓形視覺感、不浪費 ring 形狀
    for (const GridCoord& h : grid_disk(x, y, radius)) {
        if (!s.in_bounds(h.x, h.y)) continue;
        const float dxf = static_cast<float>(h.x - x);
        const float dyf = static_cast<float>(h.y - y);
        const float dist = std::sqrt(dxf * dxf + dyf * dyf);
        if (dist > static_cast<float>(radius) + 0.5f) continue;
        const float w = gaussian(dist, static_cast<float>(radius));
        s.add_h(h.x, h.y, delta * w);
    }
}

// ── Radial stamp（ridge / rift） ──────────────────────────────────────

namespace {

// 對齊 tools.py:_tile_hash —— 每格穩定的偽隨機值 [0, 1)。
[[nodiscard]] float tile_hash(int x, int y) noexcept {
    uint32_t v = static_cast<uint32_t>(x) * 1664525u
               + static_cast<uint32_t>(y) * 1013904223u;
    v ^= v >> 16;
    v *= 0x45d9f3bu;
    v ^= v >> 16;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr float kDeg2Rad = kPi / 180.0f;

// 角度差正規化到 [0, π]
[[nodiscard]] inline float angle_diff(float a, float b) noexcept {
    float d = std::fmod((a - b) + kPi, kTau);
    if (d < 0.0f) d += kTau;
    return std::abs(d - kPi);
}

void apply_radial_stamp(EditorState& s, int cx, int cy, float sign, ToolRng& rng) {
    const int   radius   = std::max(1, s.brush_size);
    const float strength = s.brush_strength * 2.5f;
    const float chaos    = s.brush_chaos;
    const float falloff  = s.brush_falloff;
    const bool  invert   = s.brush_spokes_invert;

    // Step 1: 決定 spoke 數量
    int n_spokes = s.brush_spokes;
    if (s.brush_spokes_rand) {
        const int lo = std::min(s.brush_spokes_min, s.brush_spokes_max);
        const int hi = std::max(s.brush_spokes_min, s.brush_spokes_max);
        n_spokes = rng.uniform_int(lo, hi);
    }

    // Step 2: 決定輪盤基底角度（wheel rotation）
    float wheel_rad;
    if (s.brush_wheel_rand) {
        const float lo = std::min(s.brush_wheel_min, s.brush_wheel_max);
        const float hi = std::max(s.brush_wheel_min, s.brush_wheel_max);
        wheel_rad = (hi > lo) ? rng.uniform_float(lo, hi) * kDeg2Rad
                              : lo * kDeg2Rad;
    } else {
        wheel_rad = std::max(0.0f, s.brush_wheel_angle) * kDeg2Rad;
    }

    // Step 3: 建立各 spoke 角度（含 per-spoke jitter）
    std::vector<float> spoke_angles;
    float half_w = 0.0f;
    if (n_spokes > 0) {
        spoke_angles.reserve(static_cast<std::size_t>(n_spokes));
        const float jitter_rad = s.brush_spoke_jitter * kDeg2Rad;
        const float inc = kTau / static_cast<float>(n_spokes);
        half_w = inc * 0.35f;
        for (int i = 0; i < n_spokes; ++i) {
            float base = static_cast<float>(i) * inc + wheel_rad;
            if (jitter_rad > 0.0f)
                base += rng.uniform_float(-jitter_rad, jitter_rad);
            spoke_angles.push_back(base);
        }
    }

    float max_reach = static_cast<float>(radius) * (1.0f + 0.6f * chaos);
    if (!spoke_angles.empty())
        max_reach = std::max(max_reach, static_cast<float>(radius) * 2.0f);
    const int search_r = static_cast<int>(std::ceil(max_reach)) + 1;

    for (const GridCoord& h : grid_disk(cx, cy, search_r)) {
        if (!s.in_bounds(h.x, h.y)) continue;
        const float dxf  = static_cast<float>(h.x - cx);
        const float dyf  = static_cast<float>(h.y - cy);
        const float dist = std::sqrt(dxf * dxf + dyf * dyf);

        // 徑向分量：非線性衰減 + 雜亂度
        float eff_radius;
        if (chaos > 0.0f) {
            const float noise_val = tile_hash(h.x, h.y);
            eff_radius = static_cast<float>(radius) * (1.0f + chaos * (noise_val * 2.0f - 1.0f) * 0.55f);
            if (eff_radius < 0.5f) eff_radius = 0.5f;
        } else {
            eff_radius = static_cast<float>(radius);
        }
        const float t = std::max(0.0f, 1.0f - dist / eff_radius);
        const float base_weight = (t > 0.0f) ? std::pow(t, falloff) : 0.0f;

        // 放射線分量：對每個 spoke 找最小角度差
        float spoke_w = 0.0f;
        if (!spoke_angles.empty() && dist > 0.0f) {
            const float tile_angle = std::atan2(dyf, dxf);
            float min_diff = kPi;
            for (float sa : spoke_angles) {
                const float d = angle_diff(tile_angle, sa);
                if (d < min_diff) min_diff = d;
            }
            if (min_diff < half_w) {
                const float spoke_cos = std::cos(min_diff / half_w * kPi * 0.5f);
                const float spoke_rad = std::max(0.0f, 1.0f - dist / (static_cast<float>(radius) * 2.0f));
                spoke_w = spoke_cos * spoke_cos * spoke_rad;
            }
        }

        const float effective = invert ? (base_weight - spoke_w)
                                       : std::max(base_weight, spoke_w);
        if (std::abs(effective) < 1e-6f) continue;
        s.add_h(h.x, h.y, sign * strength * effective);
    }
}

} // anonymous

void apply_ridge_stamp(EditorState& s, int x, int y, ToolRng& rng) {
    apply_radial_stamp(s, x, y, +1.0f, rng);
}

void apply_rift_stamp(EditorState& s, int x, int y, ToolRng& rng) {
    apply_radial_stamp(s, x, y, -1.0f, rng);
}

// ── Water source toggle ──────────────────────────────────────────────

void toggle_water_source(EditorState& s, int x, int y) {
    const GridCoord pt{x, y};
    auto it = std::find(s.water_sources.begin(), s.water_sources.end(), pt);
    if (it == s.water_sources.end()) s.water_sources.push_back(pt);
    else                              s.water_sources.erase(it);
}

} // namespace viewer

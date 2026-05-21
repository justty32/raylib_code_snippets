#include "tools.hpp"

#include "hex_layout.hpp"

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

void apply_brush(EditorState& s, int q, int r, float delta) {
    const int radius = std::max(1, s.brush_size);
    for (const HexCoord& h : hex_disk(q, r, radius)) {
        if (!s.in_bounds(h.q, h.r)) continue;
        const int   dist = hex_distance(q, r, h.q, h.r);
        const float w    = gaussian(static_cast<float>(dist), static_cast<float>(radius));
        s.add_h(h.q, h.r, delta * w);
    }
}

// ── Radial stamp（ridge / rift） ──────────────────────────────────────

namespace {

// 對齊 tools.py:_tile_hash —— 每格穩定的偽隨機值 [0, 1)。
[[nodiscard]] float tile_hash(int q, int r) noexcept {
    uint32_t v = static_cast<uint32_t>(q) * 1664525u
               + static_cast<uint32_t>(r) * 1013904223u;
    v ^= v >> 16;
    v *= 0x45d9f3bu;
    v ^= v >> 16;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

void apply_radial_stamp(EditorState& s, int q, int r, float sign, ToolRng& rng) {
    const int   radius   = std::max(1, s.brush_size);
    const float strength = s.brush_strength * 2.5f;
    const float chaos    = s.brush_chaos;
    const float falloff  = s.brush_falloff;
    const bool  invert   = s.brush_spokes_invert;

    // 本次 stamp 的 spokes 數量與旋轉偏移
    int   n_spokes     = s.brush_spokes;
    float spoke_offset = 0.0f;
    if (s.brush_spokes_rand) {
        const int lo = std::min(s.brush_spokes_min, s.brush_spokes_max);
        const int hi = std::max(s.brush_spokes_min, s.brush_spokes_max);
        n_spokes = rng.uniform_int(lo, hi);
        if (n_spokes > 0) spoke_offset = rng.uniform_float(0.0f, kTau);
    }

    // 搜尋半徑（chaos 擴邊界；spokes 延伸到 2× 基本半徑）
    float max_reach = static_cast<float>(radius) * (1.0f + 0.6f * chaos);
    if (n_spokes > 0) max_reach = std::max(max_reach, static_cast<float>(radius) * 2.0f);
    const int search_r = static_cast<int>(std::ceil(max_reach)) + 1;

    const float spoke_inc = (n_spokes > 0) ? (kTau / static_cast<float>(n_spokes)) : 0.0f;
    const float half_w    = spoke_inc * 0.35f;

    for (const HexCoord& h : hex_disk(q, r, search_r)) {
        if (!s.in_bounds(h.q, h.r)) continue;
        const int dist = hex_distance(q, r, h.q, h.r);

        // 徑向分量：非線性衰減 + 雜亂度
        float eff_radius;
        if (chaos > 0.0f) {
            const float noise_val = tile_hash(h.q, h.r);
            eff_radius = static_cast<float>(radius) * (1.0f + chaos * (noise_val * 2.0f - 1.0f) * 0.55f);
            if (eff_radius < 0.5f) eff_radius = 0.5f;
        } else {
            eff_radius = static_cast<float>(radius);
        }
        const float t = std::max(0.0f, 1.0f - static_cast<float>(dist) / eff_radius);
        const float base_weight = (t > 0.0f) ? std::pow(t, falloff) : 0.0f;

        // 放射線分量
        float spoke_w = 0.0f;
        if (n_spokes > 0 && dist > 0) {
            const float dq    = static_cast<float>(h.q - q);
            const float dr    = static_cast<float>(h.r - r);
            const float cx_f  = dq + 0.5f * dr;
            const float cy_f  = dr * 0.8660254f;   // sqrt(3)/2
            const float angle = std::atan2(cy_f, cx_f) - spoke_offset;
            const float idx   = std::round(angle / spoke_inc);
            const float nearest = idx * spoke_inc;
            // 角度差包到 (-π, π]
            float raw_diff = angle - nearest;
            float diff = std::fmod(raw_diff + kPi, kTau);
            if (diff < 0.0f) diff += kTau;
            diff = std::abs(diff - kPi);
            if (diff < half_w) {
                const float spoke_cos = std::cos(diff / half_w * kPi * 0.5f);
                const float spoke_rad = std::max(0.0f, 1.0f - static_cast<float>(dist) / (static_cast<float>(radius) * 2.0f));
                spoke_w = spoke_cos * spoke_cos * spoke_rad;
            }
        }

        const float effective = invert ? (base_weight - spoke_w)
                                       : std::max(base_weight, spoke_w);
        if (std::abs(effective) < 1e-6f) continue;
        s.add_h(h.q, h.r, sign * strength * effective);
    }
}

} // anonymous

void apply_ridge_stamp(EditorState& s, int q, int r, ToolRng& rng) {
    apply_radial_stamp(s, q, r, +1.0f, rng);
}

void apply_rift_stamp(EditorState& s, int q, int r, ToolRng& rng) {
    apply_radial_stamp(s, q, r, -1.0f, rng);
}

// ── Water source toggle ──────────────────────────────────────────────

void toggle_water_source(EditorState& s, int q, int r) {
    const HexCoord pt{q, r};
    auto it = std::find(s.water_sources.begin(), s.water_sources.end(), pt);
    if (it == s.water_sources.end()) s.water_sources.push_back(pt);
    else                              s.water_sources.erase(it);
}

} // namespace viewer

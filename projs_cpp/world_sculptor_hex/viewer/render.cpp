#include "render.hpp"

#include "hex_layout.hpp"

#include <algorithm>
#include <array>

namespace viewer {

namespace {

struct Band {
    float h_end;
    Color c0;   // band 起點顏色（亮）
    Color c1;   // band 終點顏色（暗）
};

// 對齊 app.py:_HEIGHT_BANDS：ocean / beach / lowland / highland / mountain / snow
constexpr std::array<Band, 6> kHeightBands = {{
    { 0.35f, {  40,  95, 195, 255}, {  10,  30,  90, 255} },  // ocean
    { 0.40f, { 205, 185, 120, 255}, { 165, 148,  95, 255} },  // beach
    { 0.58f, { 100, 178,  62, 255}, {  55, 120,  30, 255} },  // lowland
    { 0.72f, { 148, 132,  84, 255}, {  95,  85,  50, 255} },  // highland
    { 0.87f, { 132, 122, 116, 255}, {  85,  78,  74, 255} },  // mountain
    { 1.00f, { 242, 242, 246, 255}, { 185, 183, 190, 255} },  // snow
}};

[[nodiscard]] inline Color lerp_rgb(Color a, Color b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{
        static_cast<unsigned char>(a.r + (b.r - a.r) * t),
        static_cast<unsigned char>(a.g + (b.g - a.g) * t),
        static_cast<unsigned char>(a.b + (b.b - a.b) * t),
        255,
    };
}

constexpr Color kBorderColor{0, 0, 0, 25};

} // anonymous

Color height_color(float h, float sea_level) noexcept {
    const Band& ocean = kHeightBands[0];
    if (h < sea_level) {
        const float sl = std::max(sea_level, 1e-6f);
        const float t  = std::clamp(h / sl, 0.0f, 1.0f);
        return lerp_rgb(ocean.c0, ocean.c1, t);
    }

    // 陸地：起點夾到 sea_level，依陸地 bands
    float prev = sea_level;
    const float h_eff = std::max(h, sea_level);
    for (size_t i = 1; i < kHeightBands.size(); ++i) {
        const Band& b = kHeightBands[i];
        if (h_eff <= b.h_end) {
            const float bw = b.h_end - prev;
            const float t  = (bw > 0.0f) ? ((h_eff - prev) / bw) : 0.0f;
            return lerp_rgb(b.c0, b.c1, t);
        }
        prev = b.h_end;
    }
    return Color{242, 242, 246, 255};
}

void draw_world(const EditorState& s, const Camera2D& camera) {
    const float size = s.hex_size;

    // Viewport culling：算出畫面四角在 world-space 的 AABB（含一格 margin）
    const Vector2 tl = GetScreenToWorld2D({0.0f, 0.0f}, camera);
    const Vector2 br = GetScreenToWorld2D(
        {static_cast<float>(GetScreenWidth()),
         static_cast<float>(GetScreenHeight())},
        camera);
    const float margin = size * 1.2f;
    const float min_x = std::min(tl.x, br.x) - margin;
    const float max_x = std::max(tl.x, br.x) + margin;
    const float min_y = std::min(tl.y, br.y) - margin;
    const float max_y = std::max(tl.y, br.y) + margin;

    for (int r = 0; r < s.height(); ++r) {
        for (int q = 0; q < s.width(); ++q) {
            const Vector2 c = hex_to_pixel(q, r, size);
            if (c.x < min_x || c.x > max_x || c.y < min_y || c.y > max_y) continue;

            const float h    = s.get_h(q, r);
            const Color fill = height_color(h, s.sea_level);
            DrawPoly(c, 6, size, -30.0f, fill);
            DrawPolyLinesEx(c, 6, size, -30.0f, 1.0f, kBorderColor);
        }
    }

    // 水源 marker
    for (const HexCoord& w : s.water_sources) {
        const Vector2 c = hex_to_pixel(w.q, w.r, size);
        DrawCircleV(c, size * 0.35f, Color{60, 180, 255, 180});
        DrawCircleLinesV(c, size * 0.35f, Color{60, 180, 255, 255});
    }
}

} // namespace viewer

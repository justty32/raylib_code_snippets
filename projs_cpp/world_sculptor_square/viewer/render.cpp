#include "render.hpp"

#include "grid_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace viewer {

namespace {

struct Band {
    float h_end;
    Color c0;
    Color c1;
};

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

namespace {

[[nodiscard]] inline Color temp_color(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{
        static_cast<unsigned char>(std::min(1.0f, t * 2.0f) * 255.0f),
        30,
        static_cast<unsigned char>(std::min(1.0f, (1.0f - t) * 2.0f) * 255.0f),
        255
    };
}

[[nodiscard]] inline Color rain_color(float v) noexcept {
    v = std::clamp(v, 0.0f, 1.0f);
    return Color{
        static_cast<unsigned char>(20.0f * (1.0f - v)),
        static_cast<unsigned char>(80.0f + 120.0f * v),
        static_cast<unsigned char>(100.0f + 155.0f * v),
        255
    };
}

} // anonymous

void draw_world(const EditorState& s, const Camera2D& camera) {
    const float size = s.cell_size;

    // Viewport culling
    const Vector2 tl = GetScreenToWorld2D({0.0f, 0.0f}, camera);
    const Vector2 br = GetScreenToWorld2D(
        {static_cast<float>(GetScreenWidth()),
         static_cast<float>(GetScreenHeight())},
        camera);
    const float min_x = std::min(tl.x, br.x);
    const float max_x = std::max(tl.x, br.x);
    const float min_y = std::min(tl.y, br.y);
    const float max_y = std::max(tl.y, br.y);

    const int x_lo = std::max(0,              static_cast<int>(std::floor(min_x / size)));
    const int x_hi = std::min(s.width()  - 1, static_cast<int>(std::floor(max_x / size)));
    const int y_lo = std::max(0,              static_cast<int>(std::floor(min_y / size)));
    const int y_hi = std::min(s.height() - 1, static_cast<int>(std::floor(max_y / size)));

    for (int y = y_lo; y <= y_hi; ++y) {
        for (int x = x_lo; x <= x_hi; ++x) {
            Color fill;
            switch (s.overlay) {
                case Overlay::Ocean:
                    fill = s.get_ocean(x, y)
                           ? Color{30,  90, 180, 255}
                           : Color{120, 170, 80, 255};
                    break;
                case Overlay::Temperature:
                    fill = temp_color(s.get_temp(x, y));
                    break;
                case Overlay::Rainfall:
                    fill = rain_color(s.get_rain(x, y));
                    break;
                default:  // Height
                    fill = height_color(s.get_h(x, y), s.sea_level);
                    break;
            }
            const float px = static_cast<float>(x) * size;
            const float py = static_cast<float>(y) * size;
            DrawRectangle(static_cast<int>(px), static_cast<int>(py),
                          static_cast<int>(size + 0.5f), static_cast<int>(size + 0.5f), fill);
            DrawRectangleLinesEx({px, py, size, size}, 1.0f, kBorderColor);
        }
    }

    // 水源 marker
    for (const GridCoord& w : s.water_sources) {
        const Vector2 c = grid_to_pixel(w.x, w.y, size);
        DrawCircleV(c, size * 0.35f, Color{60, 180, 255, 180});
        DrawCircleLinesV(c, size * 0.35f, Color{60, 180, 255, 255});
    }
}

} // namespace viewer

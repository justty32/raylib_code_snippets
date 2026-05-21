// snippets_cpp/tilemap_hex.cpp
// Hex tilemap (pointy-top, odd-r offset coords): Camera2D pan/zoom, mouse hover, left-click cycle color.
//
// Controls:
//   Left drag          — pan (move map)
//   Scroll wheel       — zoom to cursor
//   Right click        — cycle tile color under cursor
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/tilemap_hex.cpp -o tilemap_hex.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ── Map parameters ───────────────────────────────────────────────────────────
static constexpr int   kCols    = 25;
static constexpr int   kRows    = 18;
static constexpr float kHexSize = 28.0f;   // circumradius (center → vertex)

// ── Tile palette ─────────────────────────────────────────────────────────────
static constexpr std::array<Color, 5> kPalette = {{
    {  80, 160,  60, 255 },  // 0 grass
    {  40,  90, 190, 255 },  // 1 water
    { 200, 180, 110, 255 },  // 2 sand
    { 130, 120, 110, 255 },  // 3 rock
    { 240, 240, 245, 255 },  // 4 snow
}};
static constexpr const char* kPaletteNames[] = { "grass", "water", "sand", "rock", "snow" };

static constexpr float kSqrt3 = 1.7320508075688772f;

// ── Hex coordinate helpers (pointy-top, odd-r offset) ─────────────────────────
struct HexCoord { int q, r; };

// offset coord → pixel center
static inline Vector2 hex_to_pixel(int q, int r, float size) {
    return {
        size * kSqrt3 * (static_cast<float>(q) + 0.5f * (r & 1)),
        size * 1.5f   *  static_cast<float>(r),
    };
}

// Cube rounding — clamps fractional cube (xf,yf,zf) to nearest integer cube
static HexCoord cube_round_to_hex(float xf, float yf, float zf) {
    int rx = static_cast<int>(std::lround(xf));
    int ry = static_cast<int>(std::lround(yf));
    int rz = static_cast<int>(std::lround(zf));
    const float dx = std::abs(rx - xf), dy = std::abs(ry - yf), dz = std::abs(rz - zf);
    if      (dx > dy && dx > dz)  rx = -ry - rz;
    else if (dy > dz)              ry = -rx - rz;
    else                           rz = -rx - ry;
    // cube (x,z) → offset (q,r)
    const int r2 = rz;
    const int q2 = rx + (r2 - (r2 & 1)) / 2;
    return { q2, r2 };
}

// pixel → hex (pointy-top, inverse of hex_to_pixel)
static inline HexCoord pixel_to_hex(float px, float py, float size) {
    const float lx = px / size, ly = py / size;
    const float qf = (kSqrt3 / 3.0f) * lx - (1.0f / 3.0f) * ly;
    const float rf = (2.0f / 3.0f) * ly;
    return cube_round_to_hex(qf, -qf - rf, rf);
}

static inline bool in_bounds(int q, int r) {
    return q >= 0 && q < kCols && r >= 0 && r < kRows;
}
static inline int tile_idx(int q, int r) { return r * kCols + q; }

// ── Camera zoom helper ────────────────────────────────────────────────────────
static void zoom_camera(Camera2D& cam, Vector2 pivot, float factor) {
    const Vector2 before = GetScreenToWorld2D(pivot, cam);
    cam.zoom = std::clamp(cam.zoom * factor, 0.1f, 10.0f);
    const Vector2 after  = GetScreenToWorld2D(pivot, cam);
    cam.target.x += before.x - after.x;
    cam.target.y += before.y - after.y;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Hex Tilemap");
    SetTargetFPS(60);

    std::vector<int> tiles(kCols * kRows, 0);  // all grass

    Camera2D camera{};
    camera.zoom   = 1.0f;
    camera.offset = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };
    // Center on map
    const Vector2 cmax_pixel = hex_to_pixel(kCols - 1, kRows - 1, kHexSize);
    camera.target = { cmax_pixel.x * 0.5f, cmax_pixel.y * 0.5f };

    while (!WindowShouldClose()) {
        camera.offset = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };

        const Vector2 mouse_s   = GetMousePosition();
        const Vector2 mouse_w   = GetScreenToWorld2D(mouse_s, camera);
        const HexCoord hovered  = pixel_to_hex(mouse_w.x, mouse_w.y, kHexSize);

        // Pan: left drag
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 d = GetMouseDelta();
            camera.target.x -= d.x / camera.zoom;
            camera.target.y -= d.y / camera.zoom;
        }

        // Zoom to cursor: scroll wheel
        const float wheel = GetMouseWheelMove();
        if (wheel > 0.0f) zoom_camera(camera, mouse_s, 1.15f);
        if (wheel < 0.0f) zoom_camera(camera, mouse_s, 1.0f / 1.15f);

        // Right click: cycle palette
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_bounds(hovered.q, hovered.r)) {
            int& t = tiles[tile_idx(hovered.q, hovered.r)];
            t = (t + 1) % static_cast<int>(kPalette.size());
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode2D(camera);

        // Viewport culling: approximate AABB in hex coords
        const Vector2 tl = GetScreenToWorld2D({ 0, 0 }, camera);
        const Vector2 br = GetScreenToWorld2D(
            { static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) }, camera);
        const float margin = kHexSize * 2.0f;

        for (int r = 0; r < kRows; ++r) {
            for (int q = 0; q < kCols; ++q) {
                const Vector2 c = hex_to_pixel(q, r, kHexSize);
                if (c.x < tl.x - margin || c.x > br.x + margin) continue;
                if (c.y < tl.y - margin || c.y > br.y + margin) continue;

                const Color fill = kPalette[tiles[tile_idx(q, r)]];
                // DrawPoly(center, sides=6, radius, rotation_deg, color)
                // -30° rotation → pointy-top orientation
                DrawPoly(c, 6, kHexSize, -30.0f, fill);
                DrawPolyLinesEx(c, 6, kHexSize, -30.0f, 1.0f, { 0, 0, 0, 50 });
            }
        }

        // Hover highlight
        if (in_bounds(hovered.q, hovered.r)) {
            const Vector2 c = hex_to_pixel(hovered.q, hovered.r, kHexSize);
            DrawPolyLinesEx(c, 6, kHexSize, -30.0f, 2.5f, { 255, 220, 80, 255 });
        }

        EndMode2D();

        DrawText("Hex Tilemap", 12, 12, 20, RAYWHITE);
        DrawText("LDrag=pan  Scroll=zoom  RClick=cycle color", 12, 38, 16, GRAY);
        if (in_bounds(hovered.q, hovered.r)) {
            const int ci = tiles[tile_idx(hovered.q, hovered.r)];
            DrawText(TextFormat("(%d,%d)  %s [%d]", hovered.q, hovered.r,
                                kPaletteNames[ci], ci), 12, 60, 16, RAYWHITE);
        }
        DrawFPS(12, GetScreenHeight() - 24);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

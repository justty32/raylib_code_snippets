// snippets_cpp/tilemap_square.cpp
// Square tilemap: Camera2D pan/zoom, mouse hover highlight, left-click cycle tile color.
//
// Controls:
//   Left drag          — pan (move map)
//   Scroll wheel       — zoom to cursor
//   Right click        — cycle tile color under cursor
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/tilemap_square.cpp -o tilemap_square.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ── Map parameters ───────────────────────────────────────────────────────────
static constexpr int   kCols     = 30;
static constexpr int   kRows     = 20;
static constexpr float kCellSize = 40.0f;

// ── Tile palette ─────────────────────────────────────────────────────────────
static constexpr std::array<Color, 5> kPalette = {{
    {  80, 160,  60, 255 },  // 0 grass
    {  40,  90, 190, 255 },  // 1 water
    { 200, 180, 110, 255 },  // 2 sand
    { 130, 120, 110, 255 },  // 3 rock
    { 240, 240, 245, 255 },  // 4 snow
}};
static constexpr const char* kPaletteNames[] = { "grass", "water", "sand", "rock", "snow" };

// ── Grid helpers ─────────────────────────────────────────────────────────────
struct GridCoord { int x, y; };

static inline bool in_bounds(int x, int y) {
    return x >= 0 && x < kCols && y >= 0 && y < kRows;
}
static inline int tile_idx(int x, int y) { return y * kCols + x; }

static inline GridCoord pixel_to_grid(float px, float py) {
    return { static_cast<int>(std::floor(px / kCellSize)),
             static_cast<int>(std::floor(py / kCellSize)) };
}

// ── Zoom-to-cursor helper ─────────────────────────────────────────────────────
static void zoom_camera(Camera2D& cam, Vector2 mouse_screen, float factor) {
    const Vector2 before = GetScreenToWorld2D(mouse_screen, cam);
    cam.zoom = std::clamp(cam.zoom * factor, 0.1f, 10.0f);
    const Vector2 after  = GetScreenToWorld2D(mouse_screen, cam);
    cam.target.x += before.x - after.x;
    cam.target.y += before.y - after.y;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Square Tilemap");
    SetTargetFPS(60);

    std::vector<int> tiles(kCols * kRows, 0);  // all grass initially

    Camera2D camera{};
    camera.zoom     = 1.0f;
    camera.offset   = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };
    camera.target   = { kCols * kCellSize * 0.5f, kRows * kCellSize * 0.5f };

    while (!WindowShouldClose()) {
        camera.offset = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };

        const Vector2 mouse_s  = GetMousePosition();
        const Vector2 mouse_w  = GetScreenToWorld2D(mouse_s, camera);
        const GridCoord hovered = pixel_to_grid(mouse_w.x, mouse_w.y);

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

        // Right click: cycle tile through palette
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_bounds(hovered.x, hovered.y)) {
            int& t = tiles[tile_idx(hovered.x, hovered.y)];
            t = (t + 1) % static_cast<int>(kPalette.size());
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode2D(camera);

        // Viewport culling: only iterate visible rows/columns
        const Vector2 tl = GetScreenToWorld2D({ 0, 0 }, camera);
        const Vector2 br = GetScreenToWorld2D(
            { static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) }, camera);
        const int x_lo = std::max(0,        static_cast<int>(std::floor(tl.x / kCellSize)));
        const int x_hi = std::min(kCols-1,  static_cast<int>(std::floor(br.x / kCellSize)));
        const int y_lo = std::max(0,        static_cast<int>(std::floor(tl.y / kCellSize)));
        const int y_hi = std::min(kRows-1,  static_cast<int>(std::floor(br.y / kCellSize)));

        for (int y = y_lo; y <= y_hi; ++y) {
            for (int x = x_lo; x <= x_hi; ++x) {
                const Color  fill = kPalette[tiles[tile_idx(x, y)]];
                const float  px   = static_cast<float>(x) * kCellSize;
                const float  py   = static_cast<float>(y) * kCellSize;
                DrawRectangleV({ px, py }, { kCellSize, kCellSize }, fill);
                DrawRectangleLinesEx({ px, py, kCellSize, kCellSize }, 1.0f, { 0, 0, 0, 50 });
            }
        }

        // Hover highlight
        if (in_bounds(hovered.x, hovered.y)) {
            const float px = static_cast<float>(hovered.x) * kCellSize;
            const float py = static_cast<float>(hovered.y) * kCellSize;
            DrawRectangleLinesEx({ px, py, kCellSize, kCellSize }, 2.5f, { 255, 220, 80, 255 });
        }

        EndMode2D();

        // HUD
        DrawText("Square Tilemap", 12, 12, 20, RAYWHITE);
        DrawText("LDrag=pan  Scroll=zoom  RClick=cycle color", 12, 38, 16, GRAY);
        if (in_bounds(hovered.x, hovered.y)) {
            const int ci = tiles[tile_idx(hovered.x, hovered.y)];
            DrawText(TextFormat("(%d,%d)  %s [%d]", hovered.x, hovered.y,
                                kPaletteNames[ci], ci), 12, 60, 16, RAYWHITE);
        }
        DrawFPS(12, GetScreenHeight() - 24);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

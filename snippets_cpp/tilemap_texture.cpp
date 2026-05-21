// snippets_cpp/tilemap_texture.cpp
// Square tilemap with two render modes: solid color vs texture atlas.
// Press T to toggle. The atlas is generated procedurally — no external files needed.
//
// Atlas layout: one 32×32 slot per tile type, arranged horizontally.
//   slot 0: grass  (green checker)
//   slot 1: water  (blue gradient)
//   slot 2: sand   (tan dots)
//   slot 3: rock   (gray noise)
//   slot 4: snow   (white with blue tint)
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/tilemap_texture.cpp -o tilemap_texture.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// ── Map parameters ───────────────────────────────────────────────────────────
static constexpr int   kCols     = 25;
static constexpr int   kRows     = 18;
static constexpr float kCellSize = 40.0f;

static constexpr int kTileCount   = 5;
static constexpr int kSlotPx      = 32;   // each atlas slot is 32×32 px

// ── Tile palette (color mode) ────────────────────────────────────────────────
static constexpr std::array<Color, kTileCount> kPalette = {{
    {  80, 160,  60, 255 },  // grass
    {  40,  90, 190, 255 },  // water
    { 200, 180, 110, 255 },  // sand
    { 130, 120, 110, 255 },  // rock
    { 240, 240, 245, 255 },  // snow
}};
static constexpr const char* kTileName[] = { "grass", "water", "sand", "rock", "snow" };

// ── Grid helpers ─────────────────────────────────────────────────────────────
struct GridCoord { int x, y; };
static inline bool        in_bounds(int x, int y) { return x>=0&&x<kCols&&y>=0&&y<kRows; }
static inline int         tile_idx (int x, int y) { return y*kCols+x; }
static inline GridCoord   pixel_to_grid(float px, float py) {
    return { static_cast<int>(std::floor(px / kCellSize)),
             static_cast<int>(std::floor(py / kCellSize)) };
}

// ── Generate procedural atlas Image ──────────────────────────────────────────
// Returns an Image with width = kTileCount*kSlotPx, height = kSlotPx.
// Each slot has a hand-crafted pixel pattern for that tile type.
static Image make_atlas_image() {
    const int W = kTileCount * kSlotPx;
    const int H = kSlotPx;

    // Work in RGBA bytes
    std::vector<uint8_t> px(static_cast<size_t>(W * H * 4), 255);

    auto set_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        const int i = (y * W + x) * 4;
        px[i+0] = r; px[i+1] = g; px[i+2] = b; px[i+3] = 255;
    };

    for (int slot = 0; slot < kTileCount; ++slot) {
        const int ox = slot * kSlotPx;
        for (int sy = 0; sy < kSlotPx; ++sy) {
            for (int sx = 0; sx < kSlotPx; ++sx) {
                uint8_t r, g, b;
                switch (slot) {
                    case 0: {   // grass: checker pattern
                        const bool checker = ((sx / 8 + sy / 8) & 1) == 0;
                        r = checker ? 80  : 100; g = checker ? 160 : 180; b = checker ? 60  : 80;
                    } break;
                    case 1: {   // water: horizontal gradient blue
                        const float t = static_cast<float>(sy) / (kSlotPx - 1);
                        r = 30;  g = static_cast<uint8_t>(70 + t * 40);
                        b = static_cast<uint8_t>(170 + t * 40);
                    } break;
                    case 2: {   // sand: tan dots
                        const bool dot = ((sx % 6 == 3) && (sy % 6 == 3));
                        r = dot ? 170 : 200; g = dot ? 150 : 180; b = dot ? 80 : 110;
                    } break;
                    case 3: {   // rock: pseudo-noise via XOR
                        const uint8_t noise = static_cast<uint8_t>((sx * 7 ^ sy * 13 ^ (sx+sy) * 3) & 0x1F);
                        r = 100 + noise; g = 95 + noise; b = 90 + noise;
                    } break;
                    case 4: {   // snow: white + faint blue tint
                        const uint8_t v = static_cast<uint8_t>(220 + ((sx ^ sy) & 0xF));
                        r = v; g = v; b = static_cast<uint8_t>(std::min(255, v + 20));
                    } break;
                    default: r = g = b = 128; break;
                }
                set_px(ox + sx, sy, r, g, b);
            }
        }
    }

    Image img;
    img.width   = W;
    img.height  = H;
    img.mipmaps = 1;
    img.format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    img.data    = new uint8_t[px.size()];
    std::copy(px.begin(), px.end(), static_cast<uint8_t*>(img.data));
    return img;
}

// ── Modify one slot in an existing Image (and re-upload to GPU) ───────────────
// Paints a solid color over slot `tile_type` in the atlas.
// Call UpdateTexture(tex, img.data) after this to push changes to GPU.
static void atlas_fill_slot(Image& img, int tile_type, Color color) {
    const int ox = tile_type * kSlotPx;
    for (int sy = 0; sy < kSlotPx; ++sy) {
        for (int sx = 0; sx < kSlotPx; ++sx) {
            ImageDrawPixel(&img, ox + sx, sy, color);
        }
    }
}

// ── Zoom-to-cursor ────────────────────────────────────────────────────────────
static void zoom_camera(Camera2D& cam, Vector2 pivot, float factor) {
    const Vector2 before = GetScreenToWorld2D(pivot, cam);
    cam.zoom = std::clamp(cam.zoom * factor, 0.1f, 10.0f);
    const Vector2 after  = GetScreenToWorld2D(pivot, cam);
    cam.target.x += before.x - after.x;
    cam.target.y += before.y - after.y;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Tilemap — Color vs Texture Atlas");
    SetTargetFPS(60);

    std::vector<int> tiles(kCols * kRows, 0);

    // Build atlas
    Image   atlas_img = make_atlas_image();
    Texture atlas_tex = LoadTextureFromImage(atlas_img);
    SetTextureFilter(atlas_tex, TEXTURE_FILTER_POINT);

    bool use_texture = false;

    Camera2D camera{};
    camera.zoom   = 1.0f;
    camera.offset = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };
    camera.target = { kCols * kCellSize * 0.5f, kRows * kCellSize * 0.5f };

    while (!WindowShouldClose()) {
        camera.offset = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };

        const Vector2 mouse_s = GetMousePosition();
        const Vector2 mouse_w = GetScreenToWorld2D(mouse_s, camera);
        const GridCoord hov   = pixel_to_grid(mouse_w.x, mouse_w.y);

        // Toggle render mode
        if (IsKeyPressed(KEY_T)) use_texture = !use_texture;

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

        // Right click: cycle tile type
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_bounds(hov.x, hov.y)) {
            int& t = tiles[tile_idx(hov.x, hov.y)];
            t = (t + 1) % kTileCount;
        }

        // Ctrl + right click: overwrite that tile's atlas slot at runtime
        // (demonstrates modifying a specific sprite in the atlas)
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && IsKeyDown(KEY_LEFT_CONTROL)
                && in_bounds(hov.x, hov.y)) {
            const int type = tiles[tile_idx(hov.x, hov.y)];
            atlas_fill_slot(atlas_img, type, { 160, 40, 200, 255 });   // purple override
            UpdateTexture(atlas_tex, atlas_img.data);                   // push to GPU
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode2D(camera);

        const Vector2 tl = GetScreenToWorld2D({ 0, 0 }, camera);
        const Vector2 br = GetScreenToWorld2D(
            { static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) }, camera);
        const int x_lo = std::max(0,        static_cast<int>(std::floor(tl.x / kCellSize)));
        const int x_hi = std::min(kCols-1,  static_cast<int>(std::floor(br.x / kCellSize)));
        const int y_lo = std::max(0,        static_cast<int>(std::floor(tl.y / kCellSize)));
        const int y_hi = std::min(kRows-1,  static_cast<int>(std::floor(br.y / kCellSize)));

        for (int y = y_lo; y <= y_hi; ++y) {
            for (int x = x_lo; x <= x_hi; ++x) {
                const int  type = tiles[tile_idx(x, y)];
                const float px  = static_cast<float>(x) * kCellSize;
                const float py  = static_cast<float>(y) * kCellSize;

                if (use_texture) {
                    // Source rect: the slot in the atlas for this tile type
                    const Rectangle src = {
                        static_cast<float>(type * kSlotPx), 0.0f,
                        static_cast<float>(kSlotPx), static_cast<float>(kSlotPx),
                    };
                    const Rectangle dst = { px, py, kCellSize, kCellSize };
                    DrawTexturePro(atlas_tex, src, dst, { 0, 0 }, 0.0f, WHITE);
                } else {
                    DrawRectangleV({ px, py }, { kCellSize, kCellSize }, kPalette[type]);
                }
                DrawRectangleLinesEx({ px, py, kCellSize, kCellSize }, 1.0f, { 0, 0, 0, 40 });
            }
        }

        // Hover highlight
        if (in_bounds(hov.x, hov.y)) {
            DrawRectangleLinesEx(
                { static_cast<float>(hov.x) * kCellSize,
                  static_cast<float>(hov.y) * kCellSize,
                  kCellSize, kCellSize }, 2.5f, { 255, 220, 80, 255 });
        }

        EndMode2D();

        DrawText(use_texture ? "Mode: TEXTURE ATLAS" : "Mode: SOLID COLOR", 12, 12, 20, RAYWHITE);
        DrawText("T=toggle  LDrag=pan  Scroll=zoom  RClick=cycle  Ctrl+RClick=paint atlas slot", 12, 38, 14, GRAY);
        if (in_bounds(hov.x, hov.y)) {
            DrawText(TextFormat("(%d,%d) %s", hov.x, hov.y, kTileName[tiles[tile_idx(hov.x, hov.y)]]),
                     12, 60, 16, RAYWHITE);
        }
        // Preview: atlas strip in corner
        DrawText("Atlas:", 12, GetScreenHeight() - 60, 14, GRAY);
        DrawTexture(atlas_tex, 60, GetScreenHeight() - 64, WHITE);
        DrawRectangleLinesEx({ 60, static_cast<float>(GetScreenHeight()) - 64,
                               static_cast<float>(atlas_tex.width),
                               static_cast<float>(atlas_tex.height) },
                             1.0f, GRAY);
        DrawFPS(12, GetScreenHeight() - 24);

        EndDrawing();
    }

    UnloadImage(atlas_img);     // frees atlas_img.data (the copy we allocated)
    UnloadTexture(atlas_tex);
    CloseWindow();
    return 0;
}

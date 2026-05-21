// snippets_cpp/sprite_atlas.cpp
// Sprite atlas patterns:
//   1. Build atlas from multiple Images (procedural or loaded from files).
//   2. Draw individual sprites from the atlas with DrawTexturePro.
//   3. Animate a sprite by advancing frame index each tick.
//   4. Modify a specific sprite in the atlas at runtime and re-upload.
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/sprite_atlas.cpp -o sprite_atlas.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17
//
// Keys: Space=step frame  A=auto-animate  M=modify sprite 2 in atlas  R=reset

#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// ── Atlas layout ─────────────────────────────────────────────────────────────
static constexpr int kSpriteW = 64;   // each sprite cell width  (px)
static constexpr int kSpriteH = 64;   // each sprite cell height (px)
static constexpr int kCols    = 4;    // sprites per row in atlas
static constexpr int kRows    = 2;    // rows of sprites

// ── Helper: draw a coloured solid into a sub-region of an Image ───────────────
static void image_fill_rect(Image& img, int ox, int oy, int w, int h, Color c) {
    ImageDrawRectangle(&img, ox, oy, w, h, c);
}

// ── Helper: draw a simple character frame (procedural sprite) ─────────────────
// Paints a stick-figure-ish shape into slot (col, row) of img.
// phase controls arm/leg pose (0..3).
static void draw_character_frame(Image& img, int col, int row, int phase, Color body_color) {
    const int ox = col * kSpriteW;
    const int oy = row * kSpriteH;

    // Background: transparent black
    image_fill_rect(img, ox, oy, kSpriteW, kSpriteH, { 0, 0, 0, 0 });

    // Body (torso): 12×20 centered
    const int bx = ox + (kSpriteW - 12) / 2;
    const int by = oy + 22;
    image_fill_rect(img, bx, by, 12, 20, body_color);

    // Head: 16×16
    const int hx = ox + (kSpriteW - 16) / 2;
    const int hy = oy + 6;
    image_fill_rect(img, hx, hy, 16, 16, body_color);

    // Arms: alternate up/down per phase
    const int arm_y = (phase & 1) ? by : by + 8;
    image_fill_rect(img, ox + 8,               arm_y, 10, 4, body_color);
    image_fill_rect(img, ox + kSpriteW - 18,   arm_y, 10, 4, body_color);

    // Legs: alternate left/right lean
    const int leg_offset = ((phase >> 1) & 1) ? 4 : 0;
    image_fill_rect(img, bx,         by + 20,              6, 14, body_color);
    image_fill_rect(img, bx + 6,     by + 20 + leg_offset, 6, 14, body_color);
}

// ── Build atlas Image ─────────────────────────────────────────────────────────
// Returns a kCols*kSpriteW × kRows*kSpriteH Image.
// Slots (col 0..3, row 0): walk animation frames (4 frames) for "hero"
// Slots (col 0..3, row 1): walk animation frames for "enemy"
static Image make_sprite_atlas() {
    const int W = kCols * kSpriteW;
    const int H = kRows * kSpriteH;

    Image img = GenImageColor(W, H, BLANK);   // BLANK = {0,0,0,0}

    // Row 0: hero (blue)
    for (int f = 0; f < kCols; ++f)
        draw_character_frame(img, f, 0, f, { 70, 130, 220, 255 });

    // Row 1: enemy (red)
    for (int f = 0; f < kCols; ++f)
        draw_character_frame(img, f, 1, f, { 200, 60, 60, 255 });

    return img;
}

// ── Rectangle for a specific sprite in the atlas ─────────────────────────────
static inline Rectangle sprite_rect(int col, int row) {
    return {
        static_cast<float>(col * kSpriteW),
        static_cast<float>(row * kSpriteH),
        static_cast<float>(kSpriteW),
        static_cast<float>(kSpriteH),
    };
}

// ── Sprite instance ───────────────────────────────────────────────────────────
struct Sprite {
    Vector2 pos;
    int     atlas_row;  // which animation strip (0=hero, 1=enemy)
    int     frame;      // current frame index 0..kCols-1
    float   fps;
    float   timer;
    float   scale;

    // Advance animation by dt seconds; wraps frame.
    void update(float dt) {
        timer += dt;
        if (timer >= 1.0f / fps) {
            timer -= 1.0f / fps;
            frame = (frame + 1) % kCols;
        }
    }

    // Draw this sprite using atlas texture.
    void draw(Texture2D atlas) const {
        const Rectangle src = sprite_rect(frame, atlas_row);
        const Rectangle dst = {
            pos.x, pos.y,
            static_cast<float>(kSpriteW)  * scale,
            static_cast<float>(kSpriteH) * scale,
        };
        // origin = top-left (0,0), rotation = 0
        DrawTexturePro(atlas, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);
    }
};

// ── Modify a single frame in the atlas at runtime ─────────────────────────────
// Overwrites (col, row) slot with a new pattern and re-uploads to GPU.
static void modify_sprite(Image& img, Texture2D& tex, int col, int row, Color new_color) {
    draw_character_frame(img, col, row, col, new_color);
    // Push changed pixel data to GPU — no need to re-create the texture
    UpdateTexture(tex, img.data);
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1000, 700, "Sprite Atlas");
    SetTargetFPS(60);

    Image   atlas_img = make_sprite_atlas();
    Texture atlas_tex = LoadTextureFromImage(atlas_img);
    SetTextureFilter(atlas_tex, TEXTURE_FILTER_POINT);

    bool  auto_animate = false;
    float display_scale = 3.0f;

    Sprite hero  = { {100, 200}, 0, 0, 8.0f, 0.0f, display_scale };
    Sprite enemy = { {400, 200}, 1, 0, 6.0f, 0.0f, display_scale };

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        // Advance animation
        if (IsKeyPressed(KEY_SPACE)) {
            hero.frame  = (hero.frame  + 1) % kCols;
            enemy.frame = (enemy.frame + 1) % kCols;
        }
        if (IsKeyPressed(KEY_A)) auto_animate = !auto_animate;
        if (auto_animate) {
            hero.update(dt);
            enemy.update(dt);
        }

        // Modify sprite 2 in atlas (col=2, row=0) → gold hero
        if (IsKeyPressed(KEY_M))
            modify_sprite(atlas_img, atlas_tex, 2, 0, { 230, 180, 40, 255 });

        // Reset atlas
        if (IsKeyPressed(KEY_R)) {
            UnloadImage(atlas_img);
            UnloadTexture(atlas_tex);
            atlas_img = make_sprite_atlas();
            atlas_tex = LoadTextureFromImage(atlas_img);
            SetTextureFilter(atlas_tex, TEXTURE_FILTER_POINT);
        }

        BeginDrawing();
        ClearBackground({ 40, 40, 50, 255 });

        hero.draw(atlas_tex);
        enemy.draw(atlas_tex);

        // ── Atlas preview strip (bottom) ──────────────────────────────────
        const int strip_y = GetScreenHeight() - kSpriteH * 2 - 60;
        DrawText("Atlas (raw):", 12, strip_y - 22, 16, GRAY);
        // Draw the full atlas at 2× scale
        const Rectangle atlas_src = { 0, 0,
            static_cast<float>(atlas_tex.width),
            static_cast<float>(atlas_tex.height) };
        const Rectangle atlas_dst = {
            12.0f, static_cast<float>(strip_y),
            static_cast<float>(atlas_tex.width)  * 2.0f,
            static_cast<float>(atlas_tex.height) * 2.0f,
        };
        DrawTexturePro(atlas_tex, atlas_src, atlas_dst, {0,0}, 0.0f, WHITE);
        DrawRectangleLinesEx({ atlas_dst.x, atlas_dst.y, atlas_dst.width, atlas_dst.height },
                             1.0f, GRAY);

        // Highlight the current hero frame in the atlas preview
        DrawRectangleLinesEx({
            12.0f + static_cast<float>(hero.frame * kSpriteW) * 2.0f,
            static_cast<float>(strip_y),
            static_cast<float>(kSpriteW) * 2.0f,
            static_cast<float>(kSpriteH) * 2.0f,
        }, 2.0f, { 255, 220, 60, 255 });

        DrawText("Space=step frame   A=auto-animate   M=modify frame 2   R=reset", 12, 12, 16, GRAY);
        DrawText(TextFormat("auto=%s  hero frame=%d  enemy frame=%d",
                            auto_animate ? "ON" : "OFF", hero.frame, enemy.frame),
                 12, 34, 16, RAYWHITE);
        DrawFPS(12, GetScreenHeight() - 24);

        EndDrawing();
    }

    UnloadImage(atlas_img);
    UnloadTexture(atlas_tex);
    CloseWindow();
    return 0;
}

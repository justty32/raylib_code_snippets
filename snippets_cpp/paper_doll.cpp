// snippets_cpp/paper_doll.cpp
// Paper doll system: layered sprite composition with per-layer tint recoloring.
//
// ── Core concept ─────────────────────────────────────────────────────────────
// • Each body/face part is a Texture2D drawn at the same canvas position.
// • Drawing order (index 0 = bottom, highest = top) determines visibility.
// • Parts intended to be recolored are stored as WHITE/GRAYSCALE textures:
//     DrawTexturePro(tex, src, dst, {0,0}, 0.0f, tint_color)
//   white pixels  → become tint_color
//   grey pixels   → become a darker shade of tint_color
//   transparent   → remain transparent (no effect)
// • Full-colour parts (eyes, lips) are drawn with tint=WHITE (unchanged).
// • Swapping a part: replace the Layer's texture pointer.
// • Recolouring: change Layer.tint.
//
// Controls:
//   Left / Right    — prev / next hair style  (3 variants)
//   Up   / Down     — prev / next eyebrow      (3 variants)
//   A    / D        — prev / next mouth         (3 variants)
//   H               — cycle hair  colour
//   S               — cycle skin  colour
//   B               — cycle eyebrow colour
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/paper_doll.cpp -o paper_doll.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

static constexpr int kW = 128, kH = 128;   // canvas size per part (all same)

// ── Low-level image helpers ───────────────────────────────────────────────────
// Fill a (possibly tilted/partial) ellipse with colour c.
static void fill_ellipse(Image& img, int cx, int cy, int rx, int ry, Color c) {
    for (int y = std::max(0, cy-ry); y <= std::min(kH-1, cy+ry); ++y) {
        const float dy = (float)(y - cy) / ry;
        const float hw = rx * std::sqrt(std::max(0.0f, 1.0f - dy*dy));
        for (int x = std::max(0,(int)(cx-hw)); x <= std::min(kW-1,(int)(cx+hw)); ++x)
            ImageDrawPixel(&img, x, y, c);
    }
}

// Draw a row of pixels (used for lines/brows).
static void hline(Image& img, int x0, int x1, int y, int thickness, Color c) {
    for (int t = 0; t < thickness; ++t)
        for (int x = x0; x <= x1; ++x)
            if (x >= 0 && x < kW && y+t >= 0 && y+t < kH)
                ImageDrawPixel(&img, x, y+t, c);
}

// ── Part generators (all 128×128, transparent background) ────────────────────

// Face base — WHITE ellipse (tint = skin colour when drawing)
static Image make_face_base() {
    Image img = GenImageColor(kW, kH, BLANK);
    fill_ellipse(img, 64, 74, 42, 50, WHITE);
    return img;
}

// Eyes — full colour (always drawn with WHITE tint so colours stay)
static Image make_eyes() {
    Image img = GenImageColor(kW, kH, BLANK);
    for (int ex : {46, 82}) {
        fill_ellipse(img, ex, 67, 10, 9, WHITE);              // sclera
        fill_ellipse(img, ex, 67,  7, 6, {90,145,210,255});   // iris
        fill_ellipse(img, ex, 67,  4, 3, BLACK);              // pupil
        fill_ellipse(img, ex, 64,  2, 2, WHITE);              // highlight
    }
    return img;
}

// Nose — tiny mark, full colour
static Image make_nose() {
    Image img = GenImageColor(kW, kH, BLANK);
    ImageDrawPixel(&img, 62, 82, {200,150,130,200});
    ImageDrawPixel(&img, 66, 82, {200,150,130,200});
    return img;
}

// Eyebrow variants (WHITE = tintable for brow colour)
static Image make_eyebrow(int v) {
    Image img = GenImageColor(kW, kH, BLANK);
    for (int side = 0; side < 2; ++side) {
        const int cx = side ? 82 : 46;
        const int dir = side ? 1 : -1;   // +1 right brow, -1 left brow
        switch (v) {
            case 0:  // gentle arch: higher in middle
                for (int i = -10; i <= 10; ++i) {
                    const int y = 52 + i*i/16;   // parabola — lowest in middle
                    hline(img, cx+i, cx+i, y, 3, WHITE);
                }
                break;
            case 1:  // flat, thick
                hline(img, cx-11, cx+11, 52, 4, WHITE);
                break;
            case 2:  // angry — inner end drops
                for (int i = -10; i <= 10; ++i) {
                    // For the left brow: right side (i>0) drops; mirror for right brow
                    const int y = 52 + (i * dir < 0 ? (-i*dir)/3 : 0);
                    hline(img, cx+i, cx+i, y, 3, WHITE);
                }
                break;
        }
    }
    return img;
}

// Mouth variants (fixed pink — use tint=WHITE when drawing)
static Image make_mouth(int v) {
    const Color lip = {210, 75, 90, 255};
    Image img = GenImageColor(kW, kH, BLANK);
    const int mx = 64, my = 93;
    switch (v) {
        case 0: // smile: arc dips down from corners
            for (int dx = -13; dx <= 13; ++dx) {
                const int y = my + dx*dx/22;
                hline(img, mx+dx, mx+dx, y, 3, lip);
            }
            break;
        case 1: // neutral
            hline(img, mx-12, mx+12, my, 3, lip);
            break;
        case 2: // frown: arc rises from corners
            for (int dx = -13; dx <= 13; ++dx) {
                const int y = my - dx*dx/22 + 7;
                hline(img, mx+dx, mx+dx, y, 3, lip);
            }
            break;
    }
    return img;
}

// Hair variants (WHITE = tintable for hair colour)
// Hair is drawn in two passes: hair_back (behind face) and hair_front (fringe).
// For simplicity this demo uses a single "front" layer drawn on top of the face.
static Image make_hair(int v) {
    Image img = GenImageColor(kW, kH, BLANK);
    switch (v) {
        case 0: // short bob
            fill_ellipse(img, 64, 30, 45, 34, WHITE);   // top dome
            ImageDrawRectangle(&img,  7, 30, 22, 55, WHITE);  // left side
            ImageDrawRectangle(&img, 99, 30, 22, 55, WHITE);  // right side
            break;
        case 1: // long straight
            fill_ellipse(img, 64, 28, 47, 36, WHITE);
            ImageDrawRectangle(&img,  5, 24, 20, 95, WHITE);
            ImageDrawRectangle(&img,103, 24, 20, 95, WHITE);
            break;
        case 2: // spiky
            fill_ellipse(img, 64, 36, 46, 30, WHITE);   // base
            // Spikes via triangle columns
            for (int s = 0; s < 5; ++s) {
                const int sx = 14 + s*22;
                for (int dy = 0; dy < 24; ++dy) {
                    const int hw = (24 - dy) * 6 / 24;
                    ImageDrawRectangle(&img, sx-hw, 10+dy, hw*2+1, 1, WHITE);
                }
            }
            break;
    }
    return img;
}

// ── Layer and draw system ─────────────────────────────────────────────────────

struct Layer {
    Texture2D tex;
    Color     tint    = WHITE;   // WHITE = draw as-is; any colour = recolour white pixels
    bool      visible = true;
};

// Uploads an Image to GPU and returns a Layer.  Caller owns the Texture.
static Layer make_layer(Image img, Color tint = WHITE) {
    Layer l;
    l.tex     = LoadTextureFromImage(img);
    l.tint    = tint;
    l.visible = true;
    UnloadImage(img);    // GPU copy exists — free CPU image
    SetTextureFilter(l.tex, TEXTURE_FILTER_POINT);
    return l;
}

// Draw all layers bottom-to-top at (origin) scaled by `scale`.
static void draw_doll(const std::vector<Layer*>& stack,
                      Vector2 origin, float scale) {
    for (const Layer* l : stack) {
        if (!l->visible) continue;
        const Rectangle src = { 0, 0, (float)l->tex.width, (float)l->tex.height };
        const Rectangle dst = {
            origin.x, origin.y,
            l->tex.width  * scale,
            l->tex.height * scale,
        };
        // tint=WHITE → draws as-is (full-colour layers)
        // tint=other → white/grey pixels of the source become that colour
        DrawTexturePro(l->tex, src, dst, {0.0f, 0.0f}, 0.0f, l->tint);
    }
}

// ── Colour palettes ───────────────────────────────────────────────────────────
static constexpr std::array<Color, 5> kSkinPalette = {{
    {255,220,185,255}, {240,195,145,255}, {190,140, 90,255},
    {100, 60, 30,255}, { 80,180,120,255},  // fantasy skin
}};
static constexpr std::array<Color, 7> kHairPalette = {{
    { 30, 20, 10,255}, {100, 65, 30,255}, {210,170, 80,255},
    {180, 60, 40,255}, {240,240,240,255}, {210, 90,150,255}, {80,130,220,255},
}};
static constexpr std::array<Color, 5> kBrowPalette = {{
    { 30, 20, 10,255}, {100, 65, 30,255}, {200,170, 80,255},
    { 60, 60, 60,255}, {210, 90,150,255},
}};

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(900, 680, "Paper Doll");
    SetTargetFPS(60);

    // ── Build all part variants ───────────────────────────────────────────
    // Parts that have multiple variants store an array of Layers.
    constexpr int kHairN = 3, kBrowN = 3, kMouthN = 3;

    Layer hair_layers[kHairN], brow_layers[kBrowN], mouth_layers[kMouthN];
    for (int i = 0; i < kHairN;  ++i) hair_layers [i] = make_layer(make_hair    (i), kHairPalette[1]);
    for (int i = 0; i < kBrowN;  ++i) brow_layers [i] = make_layer(make_eyebrow (i), kBrowPalette[0]);
    for (int i = 0; i < kMouthN; ++i) mouth_layers[i] = make_layer(make_mouth   (i));

    Layer face_layer = make_layer(make_face_base(), kSkinPalette[0]);
    Layer eyes_layer = make_layer(make_eyes());    // full colour, tint=WHITE
    Layer nose_layer = make_layer(make_nose());    // full colour, tint=WHITE

    int hair_idx  = 0, brow_idx  = 0, mouth_idx = 0;
    int skin_idx  = 0, hair_col  = 1, brow_col  = 0;

    // ── Draw stack: order = bottom→top ────────────────────────────────────
    // Rebuild each frame (cheap — just pointer updates)
    std::vector<Layer*> draw_stack = {
        &hair_layers [hair_idx],   // hair behind face
        &face_layer,
        &eyes_layer,
        &nose_layer,
        &brow_layers [brow_idx],
        &mouth_layers[mouth_idx],
    };

    const float kScale  = 3.5f;
    const Vector2 origin = { 220.0f, 40.0f };

    while (!WindowShouldClose()) {
        // ── Input: cycle variants ────────────────────────────────────────────
        if (IsKeyPressed(KEY_LEFT))  hair_idx  = (hair_idx  + kHairN  - 1) % kHairN;
        if (IsKeyPressed(KEY_RIGHT)) hair_idx  = (hair_idx  + 1)           % kHairN;
        if (IsKeyPressed(KEY_UP))    brow_idx  = (brow_idx  + kBrowN  - 1) % kBrowN;
        if (IsKeyPressed(KEY_DOWN))  brow_idx  = (brow_idx  + 1)           % kBrowN;
        if (IsKeyPressed(KEY_A))     mouth_idx = (mouth_idx + kMouthN - 1) % kMouthN;
        if (IsKeyPressed(KEY_D))     mouth_idx = (mouth_idx + 1)           % kMouthN;

        // ── Input: cycle colours ─────────────────────────────────────────────
        if (IsKeyPressed(KEY_S)) {
            skin_idx = (skin_idx + 1) % (int)kSkinPalette.size();
            face_layer.tint = kSkinPalette[skin_idx];
        }
        if (IsKeyPressed(KEY_H)) {
            hair_col = (hair_col + 1) % (int)kHairPalette.size();
            for (int i = 0; i < kHairN; ++i)
                hair_layers[i].tint = kHairPalette[hair_col];
        }
        if (IsKeyPressed(KEY_B)) {
            brow_col = (brow_col + 1) % (int)kBrowPalette.size();
            for (int i = 0; i < kBrowN; ++i)
                brow_layers[i].tint = kBrowPalette[brow_col];
        }

        // ── Rebuild draw stack with current selections ────────────────────────
        draw_stack = {
            &hair_layers [hair_idx],
            &face_layer,
            &eyes_layer,
            &nose_layer,
            &brow_layers [brow_idx],
            &mouth_layers[mouth_idx],
        };

        // ── Draw ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 40, 40, 50, 255 });

        draw_doll(draw_stack, origin, kScale);

        // ── HUD ───────────────────────────────────────────────────────────────
        const int tx = 10, ty = 10, ts = 15;
        DrawText("Paper Doll System", tx, ty,     20, RAYWHITE);
        DrawText("Left/Right = hair style",  tx, ty+28,  ts, GRAY);
        DrawText("Up/Down    = eyebrow",     tx, ty+46,  ts, GRAY);
        DrawText("A/D        = mouth",       tx, ty+64,  ts, GRAY);
        DrawText("H = hair colour",          tx, ty+88,  ts, YELLOW);
        DrawText("S = skin colour",          tx, ty+106, ts, YELLOW);
        DrawText("B = brow colour",          tx, ty+124, ts, YELLOW);

        DrawText(TextFormat("Hair  %d  tint=(%d,%d,%d)",
            hair_idx,
            hair_layers[hair_idx].tint.r,
            hair_layers[hair_idx].tint.g,
            hair_layers[hair_idx].tint.b), tx, ty+148, ts, LIGHTGRAY);
        DrawText(TextFormat("Brow  %d", brow_idx),  tx, ty+166, ts, LIGHTGRAY);
        DrawText(TextFormat("Mouth %d", mouth_idx), tx, ty+184, ts, LIGHTGRAY);

        DrawFPS(tx, GetScreenHeight() - 24);
        EndDrawing();
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    for (auto& l : hair_layers)  UnloadTexture(l.tex);
    for (auto& l : brow_layers)  UnloadTexture(l.tex);
    for (auto& l : mouth_layers) UnloadTexture(l.tex);
    UnloadTexture(face_layer.tex);
    UnloadTexture(eyes_layer.tex);
    UnloadTexture(nose_layer.tex);
    CloseWindow();
    return 0;
}

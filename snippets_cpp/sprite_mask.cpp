// snippets_cpp/sprite_mask.cpp
// Sprite masking / region tinting — three patterns demonstrated side by side.
//
// ── Pattern A: WHITE-BASE + TINT (most common for paper dolls) ───────────────
//   • The source texture stores only SHAPE (white/grey on transparent).
//   • DrawTexturePro(..., tint_color) recolours the white pixels.
//   • Transparent areas are skipped entirely (no colour contribution).
//   • One grayscale asset → unlimited colour variations.
//
// ── Pattern B: REGION MASK OVERLAY ────────────────────────────────────────────
//   • Start with a full-colour base sprite.
//   • Each "mask texture" is transparent everywhere EXCEPT the region
//     you want to recolour, where it is white/grey.
//   • Draw the mask on top of the base using DrawTexturePro with the
//     desired tint.  Because the mask is transparent outside the region,
//     only that region is affected.
//   • Useful when you want to recolour one part of an already-drawn sprite
//     without breaking the rest.
//
// ── Pattern C: BLEND_MULTIPLIED full-sprite overlay ──────────────────────────
//   • Draw a coloured rectangle (or texture) over the sprite with
//     BeginBlendMode(BLEND_MULTIPLIED).
//   • Each existing pixel P is multiplied: result = P * overlay / 255.
//   • white * red_overlay   = red        (full tint)
//   • grey  * red_overlay   = dark red
//   • black * anything      = black      (unchanged)
//   • Useful for damage flash, night/shadow tinting, poison glow, etc.
//   • Does NOT support per-region precision — affects the whole sprite.
//
// Keys: 1/2/3 = switch displayed pattern
//       For pattern A: H = cycle shirt color,  P = cycle pants color
//       For pattern B: M = cycle mask region tint colour
//       For pattern C: O = cycle overlay colour
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/sprite_mask.cpp -o sprite_mask.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>

static constexpr int kW = 80, kH = 128;  // sprite canvas

// ── Image helpers ─────────────────────────────────────────────────────────────
static void fill_ellipse(Image& img, int cx, int cy, int rx, int ry, Color c) {
    for (int y = std::max(0,cy-ry); y <= std::min(kH-1,cy+ry); ++y) {
        const float dy = (float)(y-cy)/ry;
        const float hw = rx * std::sqrt(std::max(0.0f,1.0f-dy*dy));
        for (int x = std::max(0,(int)(cx-hw)); x <= std::min(kW-1,(int)(cx+hw)); ++x)
            ImageDrawPixel(&img, x, y, c);
    }
}

// ── Sprite generators ────────────────────────────────────────────────────────

// ──────────────── Pattern A: white-base layers ────────────────────────────────
// Each layer = WHITE silhouette on transparent background.
// Drawing with a tint colour recolours it.

// Skin (face + hands): white ellipses for head and hands
static Image make_skin_layer() {
    Image img = GenImageColor(kW, kH, BLANK);
    fill_ellipse(img, 40, 18, 16, 17, WHITE);        // head
    fill_ellipse(img, 14, 80,  7,  9, WHITE);        // left hand
    fill_ellipse(img, 66, 80,  7,  9, WHITE);        // right hand
    return img;
}
// Shirt: torso region
static Image make_shirt_layer() {
    Image img = GenImageColor(kW, kH, BLANK);
    ImageDrawRectangle(&img, 18, 38, 44, 38, WHITE); // torso
    ImageDrawRectangle(&img,  4, 38, 16, 32, WHITE); // left arm
    ImageDrawRectangle(&img, 60, 38, 16, 32, WHITE); // right arm
    return img;
}
// Pants: lower body
static Image make_pants_layer() {
    Image img = GenImageColor(kW, kH, BLANK);
    ImageDrawRectangle(&img, 18, 74, 20, 50, WHITE); // left leg
    ImageDrawRectangle(&img, 42, 74, 20, 50, WHITE); // right leg
    return img;
}
// Eyes (full colour — drawn with tint=WHITE so they stay unchanged)
static Image make_eyes_layer() {
    Image img = GenImageColor(kW, kH, BLANK);
    for (int ex : {33, 47}) {
        fill_ellipse(img, ex, 16, 5, 4, WHITE);
        fill_ellipse(img, ex, 16, 3, 3, {60,120,200,255});
        fill_ellipse(img, ex, 16, 2, 2, BLACK);
    }
    return img;
}

// ──────────────── Pattern B helpers: full-colour base + region mask ───────────

// Full-colour base sprite (all parts drawn in their final default colours).
static Image make_base_sprite() {
    Image img = GenImageColor(kW, kH, BLANK);
    // skin
    fill_ellipse(img, 40, 18, 16, 17, {240,195,155,255});
    // shirt (blue)
    ImageDrawRectangle(&img, 18, 38, 44, 38, {60,100,200,255});
    ImageDrawRectangle(&img,  4, 38, 16, 32, {60,100,200,255});
    ImageDrawRectangle(&img, 60, 38, 16, 32, {60,100,200,255});
    // pants (dark)
    ImageDrawRectangle(&img, 18, 74, 20, 50, {40, 40, 80,255});
    ImageDrawRectangle(&img, 42, 74, 20, 50, {40, 40, 80,255});
    // hands
    fill_ellipse(img, 14, 80, 7, 9, {240,195,155,255});
    fill_ellipse(img, 66, 80, 7, 9, {240,195,155,255});
    // eyes
    for (int ex : {33, 47}) {
        fill_ellipse(img, ex, 16, 3, 3, {60,120,200,255});
        fill_ellipse(img, ex, 16, 2, 2, BLACK);
    }
    return img;
}

// Shirt region mask: WHITE only in the shirt area, transparent elsewhere.
// Drawing this on top of the base with a tint recolours just the shirt.
static Image make_shirt_mask() {
    Image img = GenImageColor(kW, kH, BLANK);
    ImageDrawRectangle(&img, 18, 38, 44, 38, WHITE);
    ImageDrawRectangle(&img,  4, 38, 16, 32, WHITE);
    ImageDrawRectangle(&img, 60, 38, 16, 32, WHITE);
    return img;
}

// Pants region mask
static Image make_pants_mask() {
    Image img = GenImageColor(kW, kH, BLANK);
    ImageDrawRectangle(&img, 18, 74, 20, 50, WHITE);
    ImageDrawRectangle(&img, 42, 74, 20, 50, WHITE);
    return img;
}

// ── Upload helper ────────────────────────────────────────────────────────────
static Texture load_tex(Image img) {
    Texture t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    return t;
}

// ── Draw a texture at (x,y) scaled by s, with tint ───────────────────────────
static void draw_sprite(Texture t, float x, float y, float s, Color tint) {
    DrawTexturePro(t,
        { 0,0,(float)t.width,(float)t.height },
        { x, y, t.width*s, t.height*s },
        {0,0}, 0.0f, tint);
}

// ── Colour cycles ─────────────────────────────────────────────────────────────
static constexpr std::array<Color,6> kShirtColors = {{
    { 60,100,200,255}, {200, 60, 60,255}, { 50,160, 80,255},
    {180,120, 30,255}, {120, 60,180,255}, { 40,160,170,255},
}};
static constexpr std::array<Color,5> kPantsColors = {{
    { 40, 40, 80,255}, {100, 70, 40,255}, { 50, 80, 50,255},
    {100,100,100,255}, { 30, 30, 30,255},
}};
static constexpr std::array<Color,5> kOverlayColors = {{
    {255, 60, 60,200}, { 60, 60,255,180}, { 60,220, 60,180},
    {255,200,  0,160}, {100, 50,255,180},
}};

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1100, 720, "Sprite Mask Techniques");
    SetTargetFPS(60);

    // Pattern A textures
    Texture skin_tex  = load_tex(make_skin_layer());
    Texture shirt_tex = load_tex(make_shirt_layer());
    Texture pants_tex = load_tex(make_pants_layer());
    Texture eyes_tex  = load_tex(make_eyes_layer());

    // Pattern B textures
    Texture base_tex       = load_tex(make_base_sprite());
    Texture shirt_mask_tex = load_tex(make_shirt_mask());
    Texture pants_mask_tex = load_tex(make_pants_mask());

    // RenderTexture for Pattern B (composite target)
    RenderTexture2D rt = LoadRenderTexture(kW, kH);

    int  pattern      = 0;      // 0=A, 1=B, 2=C
    int  shirt_col_i  = 0;
    int  pants_col_i  = 0;
    int  overlay_i    = 0;
    float scale       = 4.0f;

    const int labels_x = 12;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ONE))   pattern = 0;
        if (IsKeyPressed(KEY_TWO))   pattern = 1;
        if (IsKeyPressed(KEY_THREE)) pattern = 2;

        if (IsKeyPressed(KEY_H)) shirt_col_i  = (shirt_col_i + 1) % (int)kShirtColors.size();
        if (IsKeyPressed(KEY_P)) pants_col_i  = (pants_col_i + 1) % (int)kPantsColors.size();
        if (IsKeyPressed(KEY_O)) overlay_i    = (overlay_i   + 1) % (int)kOverlayColors.size();

        const Color shirt_col   = kShirtColors[shirt_col_i];
        const Color pants_col   = kPantsColors[pants_col_i];
        const Color overlay_col = kOverlayColors[overlay_i];

        // ── Composite Pattern B into RenderTexture ────────────────────────────
        // We draw the full-colour base, then overlay the two region masks with tints.
        BeginTextureMode(rt);
            ClearBackground(BLANK);
            // 1. Draw the base sprite (full colour, as-is)
            DrawTexture(base_tex, 0, 0, WHITE);
            // 2. Overlay shirt mask with shirt colour
            //    Transparent areas of the mask are skipped → only shirt pixels affected
            DrawTexture(shirt_mask_tex, 0, 0, shirt_col);
            // 3. Overlay pants mask with pants colour
            DrawTexture(pants_mask_tex, 0, 0, pants_col);
        EndTextureMode();

        // ── Draw ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 32, 32, 42, 255 });

        const float sx = 100.0f, sy = 100.0f;   // sprite draw origin

        if (pattern == 0) {
            // ── Pattern A: white-base layers ──────────────────────────────────
            // Draw layers bottom→top. White pixels of each layer become tint_color.
            draw_sprite(pants_tex, sx, sy, scale, pants_col);          // pants (tinted)
            draw_sprite(shirt_tex, sx, sy, scale, shirt_col);          // shirt (tinted)
            draw_sprite(skin_tex,  sx, sy, scale, {240,195,155,255});  // skin (tinted)
            draw_sprite(eyes_tex,  sx, sy, scale, WHITE);              // eyes (full colour)

            DrawText("Pattern A: WHITE-BASE + TINT", labels_x, 12, 18, RAYWHITE);
            DrawText("Each layer is a WHITE silhouette.", labels_x, 36, 14, LIGHTGRAY);
            DrawText("DrawTexturePro with tint recolours white pixels.", labels_x, 56, 14, LIGHTGRAY);
            DrawText("H = shirt colour    P = pants colour", labels_x, 80, 14, YELLOW);

        } else if (pattern == 1) {
            // ── Pattern B: full-colour base + region mask overlay ─────────────
            // Draw the composited RenderTexture (base + coloured regions).
            // RenderTexture is stored bottom-up, so negate source height to flip.
            DrawTexturePro(rt.texture,
                { 0, 0, (float)kW, -(float)kH },   // negative H flips vertical
                { sx, sy, kW * scale, kH * scale },
                {0,0}, 0.0f, WHITE);

            DrawText("Pattern B: FULL-COLOUR BASE + REGION MASK", labels_x, 12, 18, RAYWHITE);
            DrawText("Base sprite drawn normally.", labels_x, 36, 14, LIGHTGRAY);
            DrawText("Mask texture = WHITE in region, transparent elsewhere.", labels_x, 56, 14, LIGHTGRAY);
            DrawText("DrawTexture(mask, x, y, tint_color) recolours only that region.", labels_x, 76, 14, LIGHTGRAY);
            DrawText("H = shirt colour    P = pants colour", labels_x, 100, 14, YELLOW);

        } else {
            // ── Pattern C: BLEND_MULTIPLIED overlay ───────────────────────────
            // Draw base sprite first, then multiply a colour over it.
            draw_sprite(base_tex, sx, sy, scale, WHITE);

            // Apply a full-sprite multiply overlay.
            // result pixel = existing_pixel * overlay_pixel / 255
            // white×colour = colour, grey×colour = darker colour, black×anything = black
            BeginBlendMode(BLEND_MULTIPLIED);
                DrawRectangle(
                    (int)sx, (int)sy,
                    (int)(kW * scale), (int)(kH * scale),
                    overlay_col);
            EndBlendMode();

            DrawText("Pattern C: BLEND_MULTIPLIED OVERLAY", labels_x, 12, 18, RAYWHITE);
            DrawText("Draws a coloured rectangle over the sprite.", labels_x, 36, 14, LIGHTGRAY);
            DrawText("result = existing_pixel * overlay / 255", labels_x, 56, 14, LIGHTGRAY);
            DrawText("Good for: damage flash, poison, night tint.", labels_x, 76, 14, LIGHTGRAY);
            DrawText("Does NOT support per-region precision.", labels_x, 96, 14, GRAY);
            DrawText("O = cycle overlay colour", labels_x, 120, 14, YELLOW);
        }

        // Pattern tabs
        const char* tabs[] = {"1: White-base", "2: Region mask", "3: Multiply"};
        for (int i = 0; i < 3; ++i) {
            const Color c = (pattern == i) ? YELLOW : GRAY;
            DrawText(tabs[i], labels_x + i * 200, GetScreenHeight() - 40, 16, c);
        }

        DrawText("1/2/3 = switch pattern", labels_x, GetScreenHeight() - 64, 14, DARKGRAY);
        DrawFPS(labels_x, GetScreenHeight() - 24);
        EndDrawing();
    }

    UnloadRenderTexture(rt);
    UnloadTexture(skin_tex); UnloadTexture(shirt_tex);
    UnloadTexture(pants_tex); UnloadTexture(eyes_tex);
    UnloadTexture(base_tex); UnloadTexture(shirt_mask_tex); UnloadTexture(pants_mask_tex);
    CloseWindow();
    return 0;
}

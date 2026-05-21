// snippets_cpp/billboard_3d.cpp
// 3D billboard sprites: sprites that always face the camera.
//
// Demonstrates:
//   1. DrawBillboard       — full texture, always faces camera
//   2. DrawBillboardRec    — sub-region of sprite atlas (pick a frame)
//   3. Animated billboard  — advance atlas frame each tick
//   4. Modifying a specific frame in the atlas at runtime (UpdateTexture)
//
// The sprite atlas is generated procedurally (no external files).
// Each frame is a coloured blob + phase-dependent "arms".
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/billboard_3d.cpp -o billboard_3d.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17
//
// Keys: Tab=toggle cursor  Scroll/RDrag=orbit  M=modify frame 2  R=reset atlas
//       A=toggle auto-animate  Space=step frame

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// ── Atlas layout ─────────────────────────────────────────────────────────────
static constexpr int kFrameW  = 64;    // each frame width  (px)
static constexpr int kFrameH  = 64;    // each frame height (px)
static constexpr int kFrameN  = 4;     // number of animation frames

// ── Procedural atlas (same logic as sprite_atlas.cpp) ────────────────────────
static void draw_blob_frame(Image& img, int frame, Color body) {
    const int ox = frame * kFrameW;
    // Clear slot
    ImageDrawRectangle(&img, ox, 0, kFrameW, kFrameH, BLANK);
    // Body circle
    ImageDrawCircle(&img, ox + kFrameW/2, kFrameH/2 + 8, 18, body);
    // Head circle
    Color head = { (unsigned char)std::min(255,(int)body.r+40),
                   (unsigned char)std::min(255,(int)body.g+40),
                   (unsigned char)std::min(255,(int)body.b+40), 255 };
    ImageDrawCircle(&img, ox + kFrameW/2, kFrameH/2 - 14, 12, head);
    // Arms (phase)
    const int arm_dy = (frame & 1) ? -6 : 6;
    ImageDrawRectangle(&img, ox + 4,          kFrameH/2 + arm_dy, 16, 5, body);
    ImageDrawRectangle(&img, ox + kFrameW-20, kFrameH/2 - arm_dy, 16, 5, body);
    // Eyes
    ImageDrawRectangle(&img, ox + kFrameW/2 - 5, kFrameH/2 - 18, 3, 3, BLACK);
    ImageDrawRectangle(&img, ox + kFrameW/2 + 2, kFrameH/2 - 18, 3, 3, BLACK);
}

static Image make_atlas() {
    Image img = GenImageColor(kFrameN * kFrameW, kFrameH, BLANK);
    for (int f = 0; f < kFrameN; ++f)
        draw_blob_frame(img, f, { 70, 130, 220, 255 });
    return img;
}

static void modify_atlas_frame(Image& img, Texture& tex, int frame, Color new_color) {
    draw_blob_frame(img, frame, new_color);
    UpdateTexture(tex, img.data);
}

// Rectangle for frame `f` in the atlas
static inline Rectangle frame_rect(int f) {
    return { static_cast<float>(f * kFrameW), 0.0f,
             static_cast<float>(kFrameW), static_cast<float>(kFrameH) };
}

// ── Orbital camera ─────────────────────────────────────────────────────────
struct OrbitalCam {
    Vector3 target   = { 0.0f, 1.0f, 0.0f };
    float   yaw      = 180.0f;
    float   pitch    = 20.0f;
    float   distance = 14.0f;

    Camera3D to_cam() const {
        const float yr=yaw*DEG2RAD, pr=pitch*DEG2RAD;
        Camera3D c;
        c.position   = { target.x+distance*cosf(pr)*sinf(yr),
                          target.y+distance*sinf(pr),
                          target.z+distance*cosf(pr)*cosf(yr) };
        c.target     = target;
        c.up         = {0,1,0};
        c.fovy       = 45.0f;
        c.projection = CAMERA_PERSPECTIVE;
        return c;
    }
};

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "3D Billboard Sprites");
    SetTargetFPS(60);

    Image   atlas_img = make_atlas();
    Texture atlas_tex = LoadTextureFromImage(atlas_img);
    SetTextureFilter(atlas_tex, TEXTURE_FILTER_POINT);

    OrbitalCam orbit;
    bool auto_anim   = true;
    int  cur_frame   = 0;
    float anim_timer = 0.0f;
    const float fps  = 6.0f;

    // Three billboard instances at different positions
    const std::array<Vector3, 3> positions = {{
        { -3.0f, 1.0f, 0.0f },
        {  0.0f, 1.0f, 0.0f },
        {  3.0f, 1.0f, 0.0f },
    }};

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        if (IsKeyPressed(KEY_A)) auto_anim = !auto_anim;
        if (IsKeyPressed(KEY_SPACE)) cur_frame = (cur_frame+1) % kFrameN;
        if (IsKeyPressed(KEY_M))
            modify_atlas_frame(atlas_img, atlas_tex, 2, { 220, 80, 40, 255 });  // frame 2 → orange
        if (IsKeyPressed(KEY_R)) {
            UnloadImage(atlas_img);
            UnloadTexture(atlas_tex);
            atlas_img = make_atlas();
            atlas_tex = LoadTextureFromImage(atlas_img);
            SetTextureFilter(atlas_tex, TEXTURE_FILTER_POINT);
        }

        // Animate
        if (auto_anim) {
            anim_timer += dt;
            if (anim_timer >= 1.0f / fps) {
                anim_timer -= 1.0f / fps;
                cur_frame = (cur_frame + 1) % kFrameN;
            }
        }

        const Camera3D cam3d = orbit.to_cam();

        // Orbit: right drag
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            const Vector2 md = GetMouseDelta();
            orbit.yaw   += md.x * 0.25f;
            orbit.pitch  = std::clamp(orbit.pitch - md.y*0.25f, 5.0f, 85.0f);
        }
        // Zoom: scroll
        orbit.distance = std::clamp(orbit.distance - GetMouseWheelMove()*1.2f, 3.0f, 40.0f);
        // Pan: Shift + left drag
        if ((IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 md = GetMouseDelta();
            const float yr = orbit.yaw*DEG2RAD, sp = orbit.distance*0.004f;
            orbit.target.x -= (cosf(yr)*md.x - sinf(yr)*md.y)*sp;
            orbit.target.z -= (sinf(yr)*md.x + cosf(yr)*md.y)*sp;
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode3D(cam3d);

        DrawGrid(20, 1.0f);

        const Rectangle src = frame_rect(cur_frame);
        const float billboard_sz = 2.0f;   // world-space size of the billboard

        for (const Vector3& pos : positions) {
            // DrawBillboardRec: picks a sub-region from the atlas texture,
            // sizes it to billboard_sz × billboard_sz world units,
            // automatically rotates to face the camera.
            DrawBillboardRec(cam3d, atlas_tex, src, pos,
                             { billboard_sz, billboard_sz }, WHITE);

            // Shadow: a small dark circle on the ground
            DrawCircle3D({ pos.x, 0.01f, pos.z }, 0.4f,
                         { 1,0,0 }, 90.0f, { 0, 0, 0, 80 });
        }

        // ── DrawBillboard (full texture, no atlas rect) demonstration ─────
        // Place a separate sprite using the full texture at a corner position
        DrawBillboard(cam3d, atlas_tex,
                      { -6.0f, 1.0f, -2.0f }, 1.8f, WHITE);

        EndMode3D();

        // Atlas preview (bottom-left)
        const int py = GetScreenHeight() - kFrameH - 50;
        DrawText("Atlas:", 12, py - 18, 14, GRAY);
        DrawTexture(atlas_tex, 12, py, WHITE);
        // Frame highlight
        DrawRectangleLinesEx({ 12.0f + static_cast<float>(cur_frame*kFrameW), static_cast<float>(py),
                               static_cast<float>(kFrameW), static_cast<float>(kFrameH) },
                             2.0f, {255,220,60,255});

        DrawText("3D Billboard Sprites", 12, 12, 20, RAYWHITE);
        DrawText("RDrag=orbit  Scroll=zoom  Shift+LDrag=pan  A=anim  Space=step  M=modify  R=reset", 12, 38, 13, GRAY);
        DrawText(TextFormat("frame %d/%d  auto=%s", cur_frame, kFrameN-1, auto_anim?"ON":"OFF"),
                 12, 58, 16, RAYWHITE);
        DrawFPS(12, GetScreenHeight()-24);
        EndDrawing();
    }

    UnloadImage(atlas_img);
    UnloadTexture(atlas_tex);
    CloseWindow();
    return 0;
}

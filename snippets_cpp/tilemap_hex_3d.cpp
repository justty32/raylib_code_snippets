// snippets_cpp/tilemap_hex_3d.cpp
// 3D hex tilemap: each tile is a hex prism (flat-top hexagonal cylinder).
// Orbital Camera3D, mouse ray → XZ plane picking for hex under cursor.
// Left-click (cursor mode): cycle tile color.
// Keys: Tab=toggle cursor, Scroll=zoom, RDrag=orbit, MMB=pan.
//
// Controls (cursor always visible):
//   Right drag              — orbit (rotate camera around target)
//   Scroll wheel            — zoom (change distance to target)
//   Shift + left drag       — pan (translate camera target on XZ plane)
//   Left click (no Shift)   — cycle tile color under cursor
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/tilemap_hex_3d.cpp -o tilemap_hex_3d.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ── Map parameters ───────────────────────────────────────────────────────────
static constexpr int   kCols    = 20;
static constexpr int   kRows    = 14;
static constexpr float kHexSize = 0.9f;   // circumradius (center → vertex), world-space
static constexpr float kHexH    = 0.18f;  // prism height
static constexpr float kSqrt3  = 1.7320508075688772f;

// ── Palette ──────────────────────────────────────────────────────────────────
static constexpr std::array<Color,5> kPalette = {{
    { 80,160, 60,255}, { 40, 90,190,255}, {200,180,110,255},
    {130,120,110,255}, {240,240,245,255}
}};
static constexpr const char* kTileName[] = {"grass","water","sand","rock","snow"};

// ── Hex coordinate helpers (pointy-top, odd-r offset) ──────────────────────
struct HexCoord { int q, r; };
static inline bool     hx_ok  (int q,int r) { return q>=0&&q<kCols&&r>=0&&r<kRows; }
static inline int       hx_idx (int q,int r) { return r*kCols+q; }

// offset (q,r) → XZ world center (Y=0 is the top face)
static inline Vector2 hx_xz(int q, int r, float sz) {
    return {
        sz * kSqrt3 * (static_cast<float>(q) + 0.5f * (r & 1)),
        sz * 1.5f   *  static_cast<float>(r),
    };
}

// Cube-rounding to convert fractional axial → nearest hex
static HexCoord cube_round_hex(float xf, float yf, float zf) {
    int rx = (int)std::lround(xf), ry = (int)std::lround(yf), rz = (int)std::lround(zf);
    float dx=std::abs(rx-xf), dy=std::abs(ry-yf), dz=std::abs(rz-zf);
    if (dx>dy&&dx>dz) rx=-ry-rz; else if (dy>dz) ry=-rx-rz; else rz=-rx-ry;
    const int r2=rz, q2=rx+(r2-(r2&1))/2;
    return {q2, r2};
}

// XZ world position → hex coord (inverse of hx_xz)
static HexCoord xz_to_hex(float wx, float wz, float sz) {
    const float lx=wx/sz, lz=wz/sz;
    const float qf=(kSqrt3/3.0f)*lx-(1.0f/3.0f)*lz;
    const float rf=(2.0f/3.0f)*lz;
    return cube_round_hex(qf, -qf-rf, rf);
}

// ── Draw a hex prism as a fan of triangles using rlgl ────────────────────────
// center = (cx, cy, cz) is the center of the TOP face.
// size = circumradius, h = prism height.
// rotation = -30° makes it pointy-top like DrawPoly in 2D.
static void draw_hex_prism(float cx, float cy, float cz, float size, float h, Color top, Color side) {
    constexpr int N = 6;
    constexpr float kRot = -30.0f * DEG2RAD;  // pointy-top

    // Precompute top-face corners
    Vector3 top_v[N];
    for (int i = 0; i < N; ++i) {
        const float a = kRot + static_cast<float>(i) * (2.0f * PI / N);
        top_v[i] = { cx + size * cosf(a), cy, cz + size * sinf(a) };
    }
    // Bottom-face corners
    Vector3 bot_v[N];
    for (int i = 0; i < N; ++i)
        bot_v[i] = { top_v[i].x, cy - h, top_v[i].z };

    rlBegin(RL_TRIANGLES);
    rlColor4ub(top.r, top.g, top.b, top.a);

    // Top face (fan from center)
    for (int i = 0; i < N; ++i) {
        rlVertex3f(cx, cy, cz);
        rlVertex3f(top_v[i].x, top_v[i].y, top_v[i].z);
        rlVertex3f(top_v[(i+1)%N].x, top_v[(i+1)%N].y, top_v[(i+1)%N].z);
    }

    rlColor4ub(side.r, side.g, side.b, side.a);
    // Side faces (quads as 2 triangles each)
    for (int i = 0; i < N; ++i) {
        const int j = (i + 1) % N;
        rlVertex3f(top_v[i].x, top_v[i].y, top_v[i].z);
        rlVertex3f(bot_v[i].x, bot_v[i].y, bot_v[i].z);
        rlVertex3f(top_v[j].x, top_v[j].y, top_v[j].z);

        rlVertex3f(top_v[j].x, top_v[j].y, top_v[j].z);
        rlVertex3f(bot_v[i].x, bot_v[i].y, bot_v[i].z);
        rlVertex3f(bot_v[j].x, bot_v[j].y, bot_v[j].z);
    }
    rlEnd();
}

// Draw hex wireframe outline on top face only
static void draw_hex_wire(float cx, float cy, float cz, float size, Color c) {
    constexpr int N = 6;
    constexpr float kRot = -30.0f * DEG2RAD;
    for (int i = 0; i < N; ++i) {
        const float a0 = kRot + static_cast<float>(i)     * (2.0f*PI/N);
        const float a1 = kRot + static_cast<float>(i+1)   * (2.0f*PI/N);
        DrawLine3D(
            { cx + size*cosf(a0), cy, cz + size*sinf(a0) },
            { cx + size*cosf(a1), cy, cz + size*sinf(a1) }, c);
    }
}

// ── Orbital camera ────────────────────────────────────────────────────────────
struct OrbitalCam {
    Vector3 target   = { kCols*kHexSize*kSqrt3*0.5f, 0.0f, kRows*kHexSize*1.5f*0.5f };
    float   yaw      = 225.0f;
    float   pitch    = 40.0f;
    float   distance = 18.0f;

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

// Ray vs y=plane_y plane
static bool ray_vs_plane(Ray ray, float py, Vector3& hit) {
    const float d = ray.direction.y;
    if (std::abs(d) < 1e-6f) return false;
    const float t = (py - ray.position.y) / d;
    if (t < 0) return false;
    hit = { ray.position.x + ray.direction.x*t, py, ray.position.z + ray.direction.z*t };
    return true;
}

// Darken a color for side faces
static inline Color darken(Color c, float f) {
    return { (unsigned char)(c.r*f), (unsigned char)(c.g*f), (unsigned char)(c.b*f), c.a };
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "3D Hex Tilemap");
    SetTargetFPS(60);

    std::vector<int> tiles(kCols*kRows, 0);
    OrbitalCam orbit;

    while (!WindowShouldClose()) {
        const Camera3D cam3d = orbit.to_cam();
        const bool shift     = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        // ── Orbit: right drag ─────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            const Vector2 md = GetMouseDelta();
            orbit.yaw   += md.x * 0.25f;
            orbit.pitch  = std::clamp(orbit.pitch - md.y*0.25f, 5.0f, 85.0f);
        }

        // ── Zoom: scroll wheel ────────────────────────────────────────────
        orbit.distance = std::clamp(orbit.distance - GetMouseWheelMove()*1.2f, 3.0f, 60.0f);

        // ── Pan: Shift + left drag ────────────────────────────────────────
        if (shift && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 md = GetMouseDelta();
            const float yr = orbit.yaw*DEG2RAD, sp = orbit.distance*0.004f;
            orbit.target.x -= (cosf(yr)*md.x - sinf(yr)*md.y)*sp;
            orbit.target.z -= (sinf(yr)*md.x + cosf(yr)*md.y)*sp;
        }

        // ── Mouse picking ─────────────────────────────────────────────────
        HexCoord hov = {-1,-1};
        {
            const Ray ray = GetScreenToWorldRay(GetMousePosition(), cam3d);
            Vector3 hit{};
            if (ray_vs_plane(ray, 0.0f, hit)) {
                hov = xz_to_hex(hit.x, hit.z, kHexSize);
                if (!hx_ok(hov.q, hov.r)) hov = {-1,-1};
            }
        }
        // Left click (no Shift): cycle tile color
        if (!shift && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hx_ok(hov.q, hov.r)) {
            int& t = tiles[hx_idx(hov.q, hov.r)];
            t = (t+1) % (int)kPalette.size();
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode3D(cam3d);

        for (int r = 0; r < kRows; ++r) {
            for (int q = 0; q < kCols; ++q) {
                const Vector2 xz  = hx_xz(q, r, kHexSize);
                const Color   top = kPalette[tiles[hx_idx(q,r)]];
                const Color   sid = darken(top, 0.65f);
                draw_hex_prism(xz.x, 0.0f, xz.y, kHexSize - 0.04f, kHexH, top, sid);
                draw_hex_wire (xz.x, 0.001f, xz.y, kHexSize, {0,0,0,50});
            }
        }

        // Hover highlight
        if (hx_ok(hov.q, hov.r)) {
            const Vector2 xz = hx_xz(hov.q, hov.r, kHexSize);
            draw_hex_wire(xz.x, 0.002f, xz.y, kHexSize, {255,220,60,255});
        }

        DrawGrid(std::max(kCols,kRows)+4, kHexSize*kSqrt3);
        EndMode3D();

        DrawText("3D Hex Tilemap", 12, 12, 20, RAYWHITE);
        DrawText("RDrag=orbit  Scroll=zoom  Shift+LDrag=pan  LClick=cycle color", 12, 38, 14, GRAY);
        if (hx_ok(hov.q, hov.r))
            DrawText(TextFormat("(%d,%d)  %s", hov.q, hov.r, kTileName[tiles[hx_idx(hov.q,hov.r)]]),
                     12, 60, 16, RAYWHITE);
        DrawFPS(12, GetScreenHeight()-24);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

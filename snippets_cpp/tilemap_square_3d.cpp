// snippets_cpp/tilemap_square_3d.cpp
// 3D square tilemap: flat tiles on XZ plane, each tile is a colored Box.
// Orbital Camera3D (yaw/pitch/distance).
// Mouse picking: ray vs ground-plane (y=0) → grid coord.
// Left-click: cycle tile color.  Right-drag: orbit.  Scroll: zoom.
//
// Controls (cursor always visible):
//   Right drag              — orbit (rotate camera around target)
//   Scroll wheel            — zoom (change distance to target)
//   Shift + left drag       — pan (translate camera target on XZ plane)
//   Left click (no Shift)   — cycle tile color under cursor
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/tilemap_square_3d.cpp -o tilemap_square_3d.exe \
//       -Iraylib/src -Lraylib/build/raylib -lraylib \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ── Map parameters ───────────────────────────────────────────────────────────
static constexpr int   kCols    = 20;
static constexpr int   kRows    = 15;
static constexpr float kCellSz  = 1.0f;   // world-space units per tile
static constexpr float kTileH   = 0.15f;  // box height (thin slab)

// ── Tile palette ─────────────────────────────────────────────────────────────
static constexpr std::array<Color, 5> kPalette = {{
    {  80, 160,  60, 255 },  // grass
    {  40,  90, 190, 255 },  // water
    { 200, 180, 110, 255 },  // sand
    { 130, 120, 110, 255 },  // rock
    { 240, 240, 245, 255 },  // snow
}};
static constexpr const char* kTileName[] = { "grass", "water", "sand", "rock", "snow" };

static inline bool in_bounds(int x, int y) { return x>=0&&x<kCols&&y>=0&&y<kRows; }
static inline int  tile_idx (int x, int y) { return y*kCols+x; }

// ── Orbital camera ────────────────────────────────────────────────────────────
struct OrbitalCam {
    Vector3 target   = { kCols * kCellSz * 0.5f, 0.0f, kRows * kCellSz * 0.5f };
    float   yaw      = 225.0f;   // degrees
    float   pitch    = 40.0f;    // degrees (5–85)
    float   distance = 15.0f;

    Camera3D to_camera3d() const {
        const float yr = yaw   * DEG2RAD;
        const float pr = pitch * DEG2RAD;
        Camera3D cam;
        cam.position = {
            target.x + distance * cosf(pr) * sinf(yr),
            target.y + distance * sinf(pr),
            target.z + distance * cosf(pr) * cosf(yr),
        };
        cam.target     = target;
        cam.up         = { 0.0f, 1.0f, 0.0f };
        cam.fovy       = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
        return cam;
    }
};

// ── Ray vs horizontal plane y=plane_y → world hit point ──────────────────────
// Returns true if the ray intersects the plane (ray not parallel).
static bool ray_vs_plane(Ray ray, float plane_y, Vector3& hit) {
    const float denom = ray.direction.y;
    if (std::abs(denom) < 1e-6f) return false;
    const float t = (plane_y - ray.position.y) / denom;
    if (t < 0.0f) return false;
    hit = {
        ray.position.x + ray.direction.x * t,
        plane_y,
        ray.position.z + ray.direction.z * t,
    };
    return true;
}

// World hit point → grid cell (XZ, cell at (x*kCellSz, z*kCellSz))
static inline bool world_to_grid(Vector3 hit, int& gx, int& gz) {
    gx = static_cast<int>(std::floor(hit.x / kCellSz));
    gz = static_cast<int>(std::floor(hit.z / kCellSz));
    return in_bounds(gx, gz);
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "3D Square Tilemap");
    SetTargetFPS(60);

    std::vector<int> tiles(kCols * kRows, 0);

    OrbitalCam orbit;

    while (!WindowShouldClose()) {
        const Camera3D cam3d = orbit.to_camera3d();
        const bool shift     = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        // ── Orbit: right drag ─────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            const Vector2 md = GetMouseDelta();
            orbit.yaw   += md.x * 0.25f;
            orbit.pitch  = std::clamp(orbit.pitch - md.y * 0.25f, 5.0f, 85.0f);
        }

        // ── Zoom: scroll wheel ────────────────────────────────────────────
        orbit.distance = std::clamp(orbit.distance - GetMouseWheelMove() * 1.2f, 3.0f, 60.0f);

        // ── Pan: Shift + left drag (translate target on XZ plane) ─────────
        if (shift && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 md = GetMouseDelta();
            const float   yr = orbit.yaw * DEG2RAD;
            const float   sp = orbit.distance * 0.004f;
            orbit.target.x -= (cosf(yr) * md.x - sinf(yr) * md.y) * sp;
            orbit.target.z -= (sinf(yr) * md.x + cosf(yr) * md.y) * sp;
        }

        // ── Mouse picking + tile interaction ──────────────────────────────
        int hov_x = -1, hov_z = -1;
        const Ray ray = GetScreenToWorldRay(GetMousePosition(), cam3d);
        Vector3 hit{};
        if (ray_vs_plane(ray, kTileH * 0.5f, hit))
            world_to_grid(hit, hov_x, hov_z);

        // Left click without Shift: cycle tile color
        if (!shift && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_bounds(hov_x, hov_z)) {
            int& t = tiles[tile_idx(hov_x, hov_z)];
            t = (t + 1) % static_cast<int>(kPalette.size());
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode3D(cam3d);

        for (int z = 0; z < kRows; ++z) {
            for (int x = 0; x < kCols; ++x) {
                const Color  fill = kPalette[tiles[tile_idx(x, z)]];
                // Tile center in world space (Y=0 → top face at y=kTileH/2)
                const Vector3 center = {
                    (static_cast<float>(x) + 0.5f) * kCellSz,
                    kTileH * 0.5f,
                    (static_cast<float>(z) + 0.5f) * kCellSz,
                };
                DrawCube(center, kCellSz - 0.05f, kTileH, kCellSz - 0.05f, fill);
                DrawCubeWires(center, kCellSz, kTileH, kCellSz, { 0, 0, 0, 60 });
            }
        }

        // Hover highlight: draw a slightly taller wire box
        if (in_bounds(hov_x, hov_z)) {
            const Vector3 hc = {
                (static_cast<float>(hov_x) + 0.5f) * kCellSz,
                kTileH * 0.5f,
                (static_cast<float>(hov_z) + 0.5f) * kCellSz,
            };
            DrawCubeWires(hc, kCellSz, kTileH * 2.0f, kCellSz, { 255, 220, 60, 255 });
        }

        // Ground grid
        DrawGrid(std::max(kCols, kRows) + 4, kCellSz);

        EndMode3D();

        DrawText("3D Square Tilemap", 12, 12, 20, RAYWHITE);
        DrawText("RDrag=orbit  Scroll=zoom  Shift+LDrag=pan  LClick=cycle color", 12, 38, 14, GRAY);
        if (in_bounds(hov_x, hov_z)) {
            DrawText(TextFormat("(%d,%d)  %s", hov_x, hov_z,
                                kTileName[tiles[tile_idx(hov_x, hov_z)]]), 12, 60, 16, RAYWHITE);
        }
        DrawFPS(12, GetScreenHeight() - 24);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

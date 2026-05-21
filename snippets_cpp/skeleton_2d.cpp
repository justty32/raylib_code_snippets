// snippets_cpp/skeleton_2d.cpp
// 2D Skeleton system with forward kinematics and interactive pose editing.
//
// ── Core concept ─────────────────────────────────────────────────────────────
// • Each Bone stores parent index, local_angle (relative to parent direction),
//   and length.  World transforms propagate root→leaf each frame (FK).
// • A zero-length hub bone acts as shared origin so spine and legs all branch
//   from the same screen point without requiring a translation channel.
// • Click a joint circle to select it; drag to rotate that bone in world space.
//
// Controls:
//   Left-click drag  — select joint and rotate bone
//   Right-click      — deselect
//   A                — toggle idle animation
//   N                — toggle bone name labels
//   R                — reset to rest pose
//
// Build (MinGW, from repo root):
//   g++ snippets_cpp/skeleton_2d.cpp -o skeleton_2d.exe ^
//       -Iraylib/src -Lraylib/build_mingw/raylib -lraylib ^
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <vector>

static constexpr float kPi  = 3.14159265f;
static constexpr float kD2R = kPi / 180.0f;
static constexpr float kR2D = 180.0f / kPi;

// ── Bone ──────────────────────────────────────────────────────────────────────

struct Bone {
    int         parent;
    float       local_angle;  // radians; relative to parent world_angle
    float       rest_angle;
    float       length;
    const char* name;
    Color       color;

    // Computed by forward_kinematics()
    Vector2 world_head;
    Vector2 world_tail;
    float   world_angle;
};

// ── Skeleton ──────────────────────────────────────────────────────────────────

struct Skeleton {
    std::vector<Bone> bones;
    Vector2           root_pos;

    int add(int parent, float local_deg, float length,
            const char* name, Color col) {
        const float r = local_deg * kD2R;
        bones.push_back({ parent, r, r, length, name, col, {}, {}, 0.0f });
        return (int)bones.size() - 1;
    }

    void forward_kinematics() {
        for (int i = 0; i < (int)bones.size(); ++i) {
            Bone& b = bones[i];
            if (b.parent < 0) {
                b.world_head  = root_pos;
                b.world_angle = b.local_angle;
            } else {
                const Bone& p = bones[b.parent];
                b.world_head  = p.world_tail;
                b.world_angle = p.world_angle + b.local_angle;
            }
            b.world_tail = {
                b.world_head.x + cosf(b.world_angle) * b.length,
                b.world_head.y + sinf(b.world_angle) * b.length
            };
        }
    }

    void reset_pose() {
        for (auto& b : bones) b.local_angle = b.rest_angle;
    }
};

// ── Drawing ───────────────────────────────────────────────────────────────────

static void draw_bone(const Bone& b, bool selected) {
    if (b.length < 1.0f) return;  // hub is invisible

    const float hw  = std::clamp(b.length * 0.18f, 5.0f, 14.0f);
    const float ang = b.world_angle;
    const Vector2 h = b.world_head;
    const Vector2 t = b.world_tail;
    const Vector2 m = { (h.x + t.x) * 0.5f, (h.y + t.y) * 0.5f };

    // Perpendicular offset for the diamond's widest points
    const Vector2 p  = { -sinf(ang) * hw, cosf(ang) * hw };
    const Vector2 ml = { m.x + p.x, m.y + p.y };
    const Vector2 mr = { m.x - p.x, m.y - p.y };

    // Filled diamond (two triangles, each half)
    DrawTriangle(h, ml, t, ColorAlpha(b.color, 0.50f));
    DrawTriangle(h, t,  mr, ColorAlpha(b.color, 0.50f));

    // Outline
    const Color edge = selected ? YELLOW : Color{ 200, 200, 200, 150 };
    DrawLineEx(h,  ml, 1.5f, edge);
    DrawLineEx(ml, t,  1.5f, edge);
    DrawLineEx(t,  mr, 1.5f, edge);
    DrawLineEx(mr, h,  1.5f, edge);

    // Head joint (selectable)
    const float jr = hw * 0.55f;
    if (selected)
        DrawCircleV(h, jr + 3.0f, ColorAlpha(YELLOW, 0.35f));
    DrawCircleV(h, jr, selected ? YELLOW : RAYWHITE);

    // Tail joint (smaller, passive)
    DrawCircleV(t, hw * 0.30f, Color{ 150, 150, 150, 180 });
}

static void draw_skeleton(const Skeleton& sk, int sel, bool names) {
    for (int i = 0; i < (int)sk.bones.size(); ++i)
        draw_bone(sk.bones[i], i == sel);

    if (!names) return;
    for (const auto& b : sk.bones) {
        if (b.length < 1.0f) continue;
        const int mx = (int)((b.world_head.x + b.world_tail.x) * 0.5f);
        const int my = (int)((b.world_head.y + b.world_tail.y) * 0.5f);
        DrawText(b.name, mx + 5, my - 9, 9, Color{ 200, 200, 200, 170 });
    }
}

// Returns the closest bone index whose head is within 20px of the mouse.
static int pick_bone(const Skeleton& sk, Vector2 mouse) {
    static constexpr float kMaxR2 = 20.0f * 20.0f;
    int   best   = -1;
    float best_d = kMaxR2;
    for (int i = 0; i < (int)sk.bones.size(); ++i) {
        const Bone& b = sk.bones[i];
        if (b.length < 1.0f) continue;
        const float dx = mouse.x - b.world_head.x;
        const float dy = mouse.y - b.world_head.y;
        const float d2 = dx*dx + dy*dy;
        if (d2 < best_d) { best_d = d2; best = i; }
    }
    return best;
}

// ── Humanoid skeleton ─────────────────────────────────────────────────────────
//
// Coordinate convention (raylib, Y-down):
//   0°  = right,  90° = down,  -90°/270° = up,  180° = left
//
// Hub (length=0) sits at root_pos; spine goes UP (-90°).
// Legs branch from hub at ~105°/75° (down-left / down-right).
// Arms branch from chest; chest world_angle = -90° (up), so
//   ±90° local gives left/right T-pose arms.

static Skeleton build_humanoid(Vector2 center) {
    Skeleton sk;
    sk.root_pos = center;

    const Color kSpine = {  80, 170, 255, 255 };
    const Color kHead  = { 255, 215,  70, 255 };
    const Color kArm   = { 255, 140,  60, 255 };
    const Color kLeg   = {  80, 220, 120, 255 };

    //               parent   deg  len  name            color
    int hub   = sk.add( -1,    0,   0, "Hub",        kSpine);
    int spine = sk.add(hub,  -90,  60, "Spine",      kSpine);
    int chest = sk.add(spine,  0,  50, "Chest",      kSpine);
    int neck  = sk.add(chest,  0,  18, "Neck",       kHead);
              sk.add(neck,    0,  28, "Head",       kHead);

    // Arms: local ±90° from chest-up = T-pose left/right
    int luarm = sk.add(chest, -90,  38, "L.UpperArm", kArm);
    int llarm = sk.add(luarm, -20,  32, "L.ForeArm",  kArm);
              sk.add(llarm,  -10,  18, "L.Hand",     kArm);

    int ruarm = sk.add(chest,  90,  38, "R.UpperArm", kArm);
    int rlarm = sk.add(ruarm,   20, 32, "R.ForeArm",  kArm);
              sk.add(rlarm,   10,  18, "R.Hand",     kArm);

    // Legs: ~105°/75° from hub's 0° = down-left/down-right
    int lthigh = sk.add(hub,   105, 48, "L.Thigh",    kLeg);
    int lshin  = sk.add(lthigh, -15, 44, "L.Shin",    kLeg);
               sk.add(lshin,  -70,  24, "L.Foot",    kLeg);

    int rthigh = sk.add(hub,    75, 48, "R.Thigh",    kLeg);
    int rshin  = sk.add(rthigh,  15, 44, "R.Shin",    kLeg);
               sk.add(rshin,  -70,  24, "R.Foot",    kLeg);

    // All intermediate indices are used as parent args above; no orphans.
    (void)luarm; (void)llarm; (void)ruarm; (void)rlarm;
    (void)lthigh; (void)lshin; (void)rthigh; (void)rshin;

    sk.forward_kinematics();
    return sk;
}

// ── Idle animation ────────────────────────────────────────────────────────────

struct AnimTrack { int bone; float amp_deg, phase_deg; };

// Bone indices match build_humanoid insertion order:
//  0=Hub  1=Spine  2=Chest  3=Neck  4=Head
//  5=L.UpperArm  6=L.ForeArm  7=L.Hand
//  8=R.UpperArm  9=R.ForeArm  10=R.Hand
//  11=L.Thigh  12=L.Shin  13=L.Foot
//  14=R.Thigh  15=R.Shin  16=R.Foot
static constexpr AnimTrack kIdleTracks[] = {
    {  1,  3.0f,   0.0f },   // spine sway
    {  2,  2.0f,  90.0f },   // chest breathing
    {  5,  6.0f,  90.0f },   // L.UpperArm bob
    {  8,  6.0f, -90.0f },   // R.UpperArm (opposite)
    {  6,  4.0f,  45.0f },   // L.ForeArm
    {  9,  4.0f, -45.0f },   // R.ForeArm
    { 11,  5.0f, 180.0f },   // L.Thigh swing
    { 14,  5.0f,   0.0f },   // R.Thigh (opposite)
    { 12,  3.0f, 180.0f },   // L.Shin
    { 15,  3.0f,   0.0f },   // R.Shin
};

static void apply_idle(Skeleton& sk, float t) {
    for (const auto& tr : kIdleTracks) {
        if (tr.bone >= (int)sk.bones.size()) continue;
        sk.bones[tr.bone].local_angle =
            sk.bones[tr.bone].rest_angle +
            sinf(t + tr.phase_deg * kD2R) * (tr.amp_deg * kD2R);
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(960, 700, "2D Skeleton — Forward Kinematics");
    SetTargetFPS(60);

    Vector2 center = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.42f };
    Skeleton sk = build_humanoid(center);

    int   sel        = -1;
    bool  animating  = false;
    bool  show_names = true;
    float anim_time  = 0.0f;

    float drag_start_local = 0.0f;
    float drag_start_mang  = 0.0f;

    while (!WindowShouldClose()) {
        // ── Window resize ─────────────────────────────────────────────────────
        if (IsWindowResized()) {
            center       = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.42f };
            sk.root_pos  = center;
        }

        // ── Keyboard ──────────────────────────────────────────────────────────
        if (IsKeyPressed(KEY_A)) animating  = !animating;
        if (IsKeyPressed(KEY_N)) show_names = !show_names;
        if (IsKeyPressed(KEY_R)) { sk.reset_pose(); animating = false; }

        // ── Mouse ─────────────────────────────────────────────────────────────
        const Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            sel = pick_bone(sk, mouse);
            if (sel >= 0) {
                animating = false;
                drag_start_local = sk.bones[sel].local_angle;
                const Vector2& h = sk.bones[sel].world_head;
                drag_start_mang  = atan2f(mouse.y - h.y, mouse.x - h.x);
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && sel >= 0) {
            const Vector2& h = sk.bones[sel].world_head;
            const float dx = mouse.x - h.x;
            const float dy = mouse.y - h.y;
            if (dx*dx + dy*dy > 9.0f) {
                const float cur = atan2f(dy, dx);
                sk.bones[sel].local_angle = drag_start_local + (cur - drag_start_mang);
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) sel = -1;

        // ── Animation ─────────────────────────────────────────────────────────
        if (animating) {
            anim_time += GetFrameTime() * 2.5f;
            apply_idle(sk, anim_time);
        }

        // ── FK ────────────────────────────────────────────────────────────────
        sk.forward_kinematics();

        // ── Draw ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 22, 22, 32, 255 });

        // Subtle grid
        for (int x = 0; x < GetScreenWidth();  x += 40)
            DrawLine(x, 0, x, GetScreenHeight(), { 38, 38, 52, 70 });
        for (int y = 0; y < GetScreenHeight(); y += 40)
            DrawLine(0, y, GetScreenWidth(), y, { 38, 38, 52, 70 });

        // Ground line + shadow ellipse
        const float gy = center.y + 115.0f;
        DrawLineEx({ 0, gy }, { (float)GetScreenWidth(), gy }, 1.0f, { 70, 70, 100, 160 });
        DrawEllipse((int)center.x, (int)gy + 5, 55, 10, { 0, 0, 0, 70 });

        draw_skeleton(sk, sel, show_names);

        // HUD
        const int tx = 14, ts = 14;
        int ty = 14;
        DrawText("2D Skeleton System", tx, ty, 22, RAYWHITE); ty += 32;
        DrawText("Left-drag joint  = rotate bone", tx, ty, ts, GRAY); ty += 19;
        DrawText("Right-click      = deselect",     tx, ty, ts, GRAY); ty += 19;
        DrawText("A  = toggle animation",            tx, ty, ts, GRAY); ty += 19;
        DrawText("N  = toggle bone names",           tx, ty, ts, GRAY); ty += 19;
        DrawText("R  = reset pose",                  tx, ty, ts, GRAY); ty += 26;

        const char* sel_name = (sel >= 0) ? sk.bones[sel].name : "(none)";
        DrawText(TextFormat("Selected : %s", sel_name),
                 tx, ty, ts, sel >= 0 ? YELLOW : LIGHTGRAY); ty += 19;

        if (sel >= 0) {
            DrawText(TextFormat("local_ang: %.1f deg",
                sk.bones[sel].local_angle * kR2D), tx, ty, ts, YELLOW);
            ty += 19;
        }

        DrawText(TextFormat("Anim: %s", animating ? "ON" : "off"),
                 tx, ty, ts, animating ? GREEN : LIGHTGRAY);

        DrawFPS(GetScreenWidth() - 90, 12);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

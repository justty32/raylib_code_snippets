// snippets_cpp/skeleton_2d.cpp
// 2D Skeleton system: forward kinematics, inverse kinematics (CCD),
// keyframe + procedural animation, capsule skinning, and pose save/load.
//
// ── Core concepts ────────────────────────────────────────────────────────────
// • FK   — each Bone stores parent index, local_angle (relative to parent),
//          and length.  World transforms propagate root→leaf each frame.
// • IK   — drag a hand/foot tip; Cyclic Coordinate Descent rotates the bones
//          of that limb chain so the tip reaches the mouse target.
// • Anim — procedural idle (sine tracks) and a keyframe "walk" clip authored
//          as sparse per-bone offsets, sampled with linear interpolation.
// • Skin — each bone optionally carries a "flesh" radius; a capsule is drawn
//          beneath the skeleton so it reads as a body rather than sticks.
// • Pose — the full set of local_angle values can be written to / read from
//          a text file, letting you snapshot and restore custom poses.
// • Limit— optional per-joint [lo, hi] range (relative to rest) so FK drags
//          and IK solves stay plausible; the rest angle is always inside it.
//
// Controls:
//   Left-drag joint  — FK mode: rotate the picked bone
//                      IK mode: drag the picked limb tip (CCD solve)
//   Right-click      — deselect
//   I                — toggle FK / IK mode
//   A                — cycle animation: off → idle → walk
//   K                — toggle skin (flesh capsules)
//   N                — toggle bone name labels
//   R                — reset to rest pose
//   L                — toggle joint angle limits
//   F5               — save current pose to skeleton_pose.txt
//   F9               — load pose from skeleton_pose.txt
//
// Build (Linux, system raylib):
//   g++ snippets_cpp/skeleton_2d.cpp -o skeleton_2d $(pkg-config --cflags --libs raylib) -std=c++17
//
// Build (Windows MinGW, from repo root):
//   g++ snippets_cpp/skeleton_2d.cpp -o skeleton_2d.exe ^
//       -Iraylib/src -Lraylib/build_mingw/raylib -lraylib ^
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

static constexpr float kPi  = 3.14159265f;
static constexpr float kTau = 2.0f * kPi;
static constexpr float kD2R = kPi / 180.0f;
static constexpr float kR2D = 180.0f / kPi;

// Wrap an angle delta into [-pi, pi] (shortest rotation).
static float wrap_pi(float a) {
    while (a >  kPi) a -= kTau;
    while (a < -kPi) a += kTau;
    return a;
}

// ── Bone ──────────────────────────────────────────────────────────────────────

struct Bone {
    int         parent;
    float       local_angle;  // radians; relative to parent world_angle
    float       rest_angle;
    float       length;
    const char* name;
    Color       color;
    float       skin;         // flesh capsule radius; 0 = no skin

    bool        limited;      // enforce [lo, hi] on local_angle?
    float       lo, hi;       // absolute local_angle bounds (radians)

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
            const char* name, Color col, float skin = 0.0f) {
        const float r = local_deg * kD2R;
        bones.push_back({ parent, r, r, length, name, col, skin,
                          false, 0.0f, 0.0f, {}, {}, 0.0f });
        return (int)bones.size() - 1;
    }

    // Constrain a bone's local_angle to [rest+lo_off, rest+hi_off] (degrees).
    // lo_off <= 0 <= hi_off keeps the rest pose valid.
    void limit(int i, float lo_off_deg, float hi_off_deg) {
        bones[i].limited = true;
        bones[i].lo = bones[i].rest_angle + lo_off_deg * kD2R;
        bones[i].hi = bones[i].rest_angle + hi_off_deg * kD2R;
    }

    void clamp_joint(int i) {
        Bone& b = bones[i];
        if (!b.limited) return;
        // Map to the equivalent angle nearest rest, then clamp to the range.
        b.local_angle = b.rest_angle + wrap_pi(b.local_angle - b.rest_angle);
        b.local_angle = std::clamp(b.local_angle, b.lo, b.hi);
    }

    void clamp_all() {
        for (int i = 0; i < (int)bones.size(); ++i) clamp_joint(i);
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

// ── Inverse kinematics (Cyclic Coordinate Descent) ──────────────────────────────
//
// A chain is the leaf "effector" bone and a "base" ancestor.  CCD repeatedly
// walks effector→base, rotating each joint so the effector tip points a little
// closer to the target.  Re-running FK after every joint keeps world transforms
// exact; the skeleton is tiny, so the cost is negligible.

struct IKChain { int effector, base; const char* name; };

static void solve_ik_ccd(Skeleton& sk, const IKChain& c, Vector2 target,
                         bool limits = true, int iters = 12) {
    for (int it = 0; it < iters; ++it) {
        int b = c.effector;
        while (true) {
            sk.forward_kinematics();
            const Vector2 tip   = sk.bones[c.effector].world_tail;
            const Vector2 joint = sk.bones[b].world_head;

            const float a_tip = atan2f(tip.y    - joint.y, tip.x    - joint.x);
            const float a_tgt = atan2f(target.y - joint.y, target.x - joint.x);
            sk.bones[b].local_angle += wrap_pi(a_tgt - a_tip);
            if (limits) sk.clamp_joint(b);

            if (b == c.base) break;
            b = sk.bones[b].parent;
        }
    }
    sk.forward_kinematics();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static Color darken(Color c, float f) {  // f in 0..1, multiplies RGB
    return { (unsigned char)(c.r * f),
             (unsigned char)(c.g * f),
             (unsigned char)(c.b * f), c.a };
}

// A capsule: a thick line (rectangle) capped by two circles.
static void draw_capsule(Vector2 a, Vector2 b, float r, Color c) {
    DrawLineEx(a, b, r * 2.0f, c);
    DrawCircleV(a, r, c);
    DrawCircleV(b, r, c);
}

static void draw_skin(const Skeleton& sk) {
    for (const auto& b : sk.bones) {
        if (b.skin <= 0.0f || b.length < 1.0f) continue;
        draw_capsule(b.world_head, b.world_tail, b.skin, darken(b.color, 0.45f));
    }
}

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

static void draw_skeleton(const Skeleton& sk, int sel, bool names, bool skin) {
    if (skin) draw_skin(sk);

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

// Draw a selected joint's allowed range as a wedge, plus its current angle.
static void draw_joint_limit(const Skeleton& sk, int i) {
    const Bone& b = sk.bones[i];
    if (!b.limited) return;
    const float base = b.world_angle - b.local_angle;  // parent world angle
    const float r    = std::clamp(b.length * 0.6f, 24.0f, 42.0f);
    const Vector2 c  = b.world_head;

    DrawCircleSector(c, r, (base + b.lo) * kR2D, (base + b.hi) * kR2D,
                     24, ColorAlpha(YELLOW, 0.12f));

    auto spoke = [&](float ang, float th, Color col) {
        DrawLineEx(c, { c.x + cosf(ang) * r, c.y + sinf(ang) * r }, th, col);
    };
    spoke(base + b.lo,    1.5f, ColorAlpha(YELLOW, 0.55f));
    spoke(base + b.hi,    1.5f, ColorAlpha(YELLOW, 0.55f));
    spoke(b.world_angle,  2.0f, ORANGE);                  // current angle
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

    //               parent   deg  len  name           color   skin
    int hub   = sk.add( -1,    0,   0, "Hub",        kSpine, 0);
    int spine = sk.add(hub,  -90,  60, "Spine",      kSpine, 16);
    int chest = sk.add(spine,  0,  50, "Chest",      kSpine, 20);
    int neck  = sk.add(chest,  0,  18, "Neck",       kHead,  8);
              sk.add(neck,    0,  28, "Head",       kHead,  18);

    // Arms: local ±90° from chest-up = T-pose left/right
    int luarm = sk.add(chest, -90,  38, "L.UpperArm", kArm, 11);
    int llarm = sk.add(luarm, -20,  32, "L.ForeArm",  kArm,  9);
              sk.add(llarm,  -10,  18, "L.Hand",     kArm,  7);

    int ruarm = sk.add(chest,  90,  38, "R.UpperArm", kArm, 11);
    int rlarm = sk.add(ruarm,   20, 32, "R.ForeArm",  kArm,  9);
              sk.add(rlarm,   10,  18, "R.Hand",     kArm,  7);

    // Legs: ~105°/75° from hub's 0° = down-left/down-right
    int lthigh = sk.add(hub,   105, 48, "L.Thigh",    kLeg, 14);
    int lshin  = sk.add(lthigh, -15, 44, "L.Shin",    kLeg, 11);
    int lfoot  = sk.add(lshin,  -70, 24, "L.Foot",    kLeg,  8);

    int rthigh = sk.add(hub,    75, 48, "R.Thigh",    kLeg, 14);
    int rshin  = sk.add(rthigh,  15, 44, "R.Shin",    kLeg, 11);
    int rfoot  = sk.add(rshin,  -70, 24, "R.Foot",    kLeg,  8);

    // Joint limits, in degrees relative to each bone's rest angle (tunable).
    // Left/right pairs are mirrored.  Hub/spine/hands stay free.
    sk.limit(neck,   -35,  35);
    sk.limit(luarm, -120, 120);   sk.limit(ruarm, -120, 120);  // shoulders (loose)
    sk.limit(llarm, -130,  15);   sk.limit(rlarm,  -15, 130);  // elbows (one-way)
    sk.limit(lthigh, -55,  55);   sk.limit(rthigh, -55,  55);  // hips
    sk.limit(lshin, -110,  15);   sk.limit(rshin,  -15, 110);  // knees (one-way)
    sk.limit(lfoot,  -45,  45);   sk.limit(rfoot,  -45,  45);  // ankles
    sk.limit(chest,  -35,  35);

    sk.forward_kinematics();
    return sk;
}

// Bone indices match build_humanoid insertion order:
//  0=Hub  1=Spine  2=Chest  3=Neck  4=Head
//  5=L.UpperArm  6=L.ForeArm  7=L.Hand
//  8=R.UpperArm  9=R.ForeArm  10=R.Hand
//  11=L.Thigh  12=L.Shin  13=L.Foot
//  14=R.Thigh  15=R.Shin  16=R.Foot
enum {
    B_HUB, B_SPINE, B_CHEST, B_NECK, B_HEAD,
    B_LUARM, B_LFARM, B_LHAND,
    B_RUARM, B_RFARM, B_RHAND,
    B_LTHIGH, B_LSHIN, B_LFOOT,
    B_RTHIGH, B_RSHIN, B_RFOOT
};

// IK chains: drag the effector's tip, solve down to the base joint.
static const IKChain kChains[] = {
    { B_LHAND, B_LUARM,  "L.Arm" },
    { B_RHAND, B_RUARM,  "R.Arm" },
    { B_LFOOT, B_LTHIGH, "L.Leg" },
    { B_RFOOT, B_RTHIGH, "R.Leg" },
};
static constexpr int kChainCount = (int)(sizeof(kChains) / sizeof(kChains[0]));

// Nearest IK effector tip within radius; returns chain index or -1.
static int pick_chain(const Skeleton& sk, Vector2 mouse) {
    static constexpr float kMaxR2 = 24.0f * 24.0f;
    int   best   = -1;
    float best_d = kMaxR2;
    for (int i = 0; i < kChainCount; ++i) {
        const Vector2 tip = sk.bones[kChains[i].effector].world_tail;
        const float dx = mouse.x - tip.x;
        const float dy = mouse.y - tip.y;
        const float d2 = dx*dx + dy*dy;
        if (d2 < best_d) { best_d = d2; best = i; }
    }
    return best;
}

// ── Procedural idle animation ───────────────────────────────────────────────────

struct AnimTrack { int bone; float amp_deg, phase_deg; };

static constexpr AnimTrack kIdleTracks[] = {
    { B_SPINE,  3.0f,   0.0f },   // spine sway
    { B_CHEST,  2.0f,  90.0f },   // chest breathing
    { B_LUARM,  6.0f,  90.0f },   // L.UpperArm bob
    { B_RUARM,  6.0f, -90.0f },   // R.UpperArm (opposite)
    { B_LFARM,  4.0f,  45.0f },   // L.ForeArm
    { B_RFARM,  4.0f, -45.0f },   // R.ForeArm
    { B_LTHIGH, 5.0f, 180.0f },   // L.Thigh swing
    { B_RTHIGH, 5.0f,   0.0f },   // R.Thigh (opposite)
    { B_LSHIN,  3.0f, 180.0f },   // L.Shin
    { B_RSHIN,  3.0f,   0.0f },   // R.Shin
};

static void apply_idle(Skeleton& sk, float t) {
    for (const auto& tr : kIdleTracks) {
        if (tr.bone >= (int)sk.bones.size()) continue;
        sk.bones[tr.bone].local_angle =
            sk.bones[tr.bone].rest_angle +
            sinf(t + tr.phase_deg * kD2R) * (tr.amp_deg * kD2R);
    }
}

// ── Keyframe "walk" clip ─────────────────────────────────────────────────────────
//
// A front-facing march-in-place: knees lift alternately while the arms
// counter-swing and the torso bobs.  Each key lists only the bones it changes,
// as a degree offset from that bone's rest angle; unlisted bones stay at rest.
// Offsets (not absolute angles) are interpolated linearly, so wrap-around is a
// non-issue.  Values are tuned by eye — tweak freely.

struct KeyOff  { int bone; float deg; };
struct WalkKey { float t; std::vector<KeyOff> offs; };

static const WalkKey kWalk[] = {
    // t=0.00 — left knee up, right leg planted
    { 0.00f, {
        { B_LTHIGH, -34 }, { B_LSHIN, -55 }, { B_LFOOT, 25 },
        { B_RTHIGH,  10 }, { B_RSHIN,   5 },
        { B_LUARM,  16 },  { B_RUARM, -16 }, { B_LFARM, 12 }, { B_RFARM, -12 },
        { B_SPINE,   2 },  { B_CHEST,  -2 },
    }},
    // t=0.25 — passing (both legs near rest, slight bob)
    { 0.25f, {
        { B_LUARM,   0 },  { B_RUARM,   0 },
        { B_CHEST,   2 },
    }},
    // t=0.50 — right knee up, left leg planted (mirror of t=0)
    { 0.50f, {
        { B_RTHIGH,  34 }, { B_RSHIN,  55 }, { B_RFOOT, -25 },
        { B_LTHIGH, -10 }, { B_LSHIN,  -5 },
        { B_LUARM, -16 },  { B_RUARM,  16 }, { B_LFARM, -12 }, { B_RFARM, 12 },
        { B_SPINE,  -2 },  { B_CHEST,  -2 },
    }},
    // t=0.75 — passing
    { 0.75f, {
        { B_LUARM,   0 },  { B_RUARM,   0 },
        { B_CHEST,   2 },
    }},
};
static constexpr int kWalkCount = (int)(sizeof(kWalk) / sizeof(kWalk[0]));

// Expand a sparse key into a full per-bone offset array (radians).
static std::vector<float> key_offsets(const WalkKey& k, int n) {
    std::vector<float> o(n, 0.0f);
    for (const auto& ko : k.offs)
        if (ko.bone >= 0 && ko.bone < n) o[ko.bone] = ko.deg * kD2R;
    return o;
}

// Sample the looping walk clip at phase in [0,1) and apply to the skeleton.
static void apply_walk(Skeleton& sk, float phase) {
    const int n = (int)sk.bones.size();
    phase -= floorf(phase);  // wrap into [0,1)

    // Find the surrounding keys (with wrap from last → first at t+1).
    int i0 = kWalkCount - 1;
    for (int i = 0; i < kWalkCount; ++i) {
        if (kWalk[i].t <= phase) i0 = i; else break;
    }
    const int   i1 = (i0 + 1) % kWalkCount;
    const float t0 = kWalk[i0].t;
    const float t1 = (i1 == 0) ? 1.0f : kWalk[i1].t;
    const float span = (t1 - t0);
    const float u = span > 1e-5f ? (phase - t0) / span : 0.0f;

    const std::vector<float> a = key_offsets(kWalk[i0], n);
    const std::vector<float> b = key_offsets(kWalk[i1], n);

    for (int i = 0; i < n; ++i)
        sk.bones[i].local_angle = sk.bones[i].rest_angle + (a[i] + (b[i] - a[i]) * u);
}

// ── Pose save / load ──────────────────────────────────────────────────────────
//
// Plain-text format: a header line, the bone count, then one local_angle
// (degrees) per line.  Round-trips through any text editor.

static const char* kPoseFile = "skeleton_pose.txt";

static bool save_pose(const Skeleton& sk, const char* path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# skeleton_2d pose (local angles, degrees)\n";
    f << sk.bones.size() << "\n";
    for (const auto& b : sk.bones)
        f << (b.local_angle * kR2D) << "\n";
    return (bool)f;
}

static bool load_pose(Skeleton& sk, const char* path) {
    std::ifstream f(path);
    if (!f) return false;

    // Skip comment lines beginning with '#'.
    std::string line;
    auto next_value = [&](float& out) -> bool {
        while (std::getline(f, line)) {
            size_t s = line.find_first_not_of(" \t\r\n");
            if (s == std::string::npos || line[s] == '#') continue;
            try { out = std::stof(line.substr(s)); return true; }
            catch (...) { return false; }
        }
        return false;
    };

    float count = 0.0f;
    if (!next_value(count)) return false;
    if ((int)count != (int)sk.bones.size()) return false;  // mismatched rig

    for (auto& b : sk.bones) {
        float deg = 0.0f;
        if (!next_value(deg)) return false;
        b.local_angle = deg * kD2R;
    }
    return true;
}

// ── main ──────────────────────────────────────────────────────────────────────

enum AnimMode { ANIM_OFF, ANIM_IDLE, ANIM_WALK, ANIM_COUNT };
static const char* anim_name(int m) {
    switch (m) {
        case ANIM_IDLE: return "idle (procedural)";
        case ANIM_WALK: return "walk (keyframe)";
        default:        return "off";
    }
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(960, 700, "2D Skeleton — FK / IK / Anim / Skin");
    SetTargetFPS(60);

    Vector2 center = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.42f };
    Skeleton sk = build_humanoid(center);

    int   sel        = -1;     // FK-selected bone
    int   sel_chain  = -1;     // IK-selected chain
    bool  ik_mode    = false;
    int   anim       = ANIM_OFF;
    bool  show_names = true;
    bool  show_skin  = true;
    bool  use_limits = true;
    float anim_time  = 0.0f;

    float drag_start_local = 0.0f;
    float drag_start_mang  = 0.0f;

    const char* status   = "";
    float       status_t = 0.0f;
    auto set_status = [&](const char* s) { status = s; status_t = 2.5f; };

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        // ── Window resize ─────────────────────────────────────────────────────
        if (IsWindowResized()) {
            center      = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.42f };
            sk.root_pos = center;
        }

        // ── Keyboard ──────────────────────────────────────────────────────────
        if (IsKeyPressed(KEY_I)) { ik_mode = !ik_mode; sel = sel_chain = -1; }
        if (IsKeyPressed(KEY_A)) anim       = (anim + 1) % ANIM_COUNT;
        if (IsKeyPressed(KEY_K)) show_skin  = !show_skin;
        if (IsKeyPressed(KEY_N)) show_names = !show_names;
        if (IsKeyPressed(KEY_L)) { use_limits = !use_limits; if (use_limits) sk.clamp_all(); }
        if (IsKeyPressed(KEY_R)) { sk.reset_pose(); anim = ANIM_OFF; sel = sel_chain = -1; }

        if (IsKeyPressed(KEY_F5))
            set_status(save_pose(sk, kPoseFile) ? "Pose saved" : "Save failed");
        if (IsKeyPressed(KEY_F9)) {
            const bool ok = load_pose(sk, kPoseFile);
            if (ok) anim = ANIM_OFF;
            set_status(ok ? "Pose loaded" : "Load failed (no/!match file)");
        }

        // ── Mouse ─────────────────────────────────────────────────────────────
        const Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (ik_mode) {
                sel_chain = pick_chain(sk, mouse);
                if (sel_chain >= 0) anim = ANIM_OFF;
            } else {
                sel = pick_bone(sk, mouse);
                if (sel >= 0) {
                    anim = ANIM_OFF;
                    drag_start_local = sk.bones[sel].local_angle;
                    const Vector2& h = sk.bones[sel].world_head;
                    drag_start_mang  = atan2f(mouse.y - h.y, mouse.x - h.x);
                }
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (ik_mode && sel_chain >= 0) {
                solve_ik_ccd(sk, kChains[sel_chain], mouse, use_limits);
            } else if (!ik_mode && sel >= 0) {
                const Vector2& h = sk.bones[sel].world_head;
                const float dx = mouse.x - h.x;
                const float dy = mouse.y - h.y;
                if (dx*dx + dy*dy > 9.0f) {
                    const float cur = atan2f(dy, dx);
                    sk.bones[sel].local_angle = drag_start_local + (cur - drag_start_mang);
                    if (use_limits) sk.clamp_joint(sel);
                }
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { sel = sel_chain = -1; }

        // ── Animation ─────────────────────────────────────────────────────────
        if (anim == ANIM_IDLE) {
            anim_time += dt * 2.5f;
            apply_idle(sk, anim_time);
        } else if (anim == ANIM_WALK) {
            anim_time += dt * 1.6f;            // ~0.63s per full cycle
            apply_walk(sk, anim_time);
        }
        if (anim != ANIM_OFF && use_limits) sk.clamp_all();

        if (status_t > 0.0f) status_t -= dt;

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

        draw_skeleton(sk, sel, show_names, show_skin);

        // Selected joint's allowed range (FK mode)
        if (!ik_mode && sel >= 0 && use_limits)
            draw_joint_limit(sk, sel);

        // IK effector handles
        if (ik_mode) {
            for (int i = 0; i < kChainCount; ++i) {
                const Vector2 tip = sk.bones[kChains[i].effector].world_tail;
                const bool on = (i == sel_chain);
                DrawCircleLinesV(tip, on ? 11.0f : 8.0f, on ? YELLOW : SKYBLUE);
                DrawCircleV(tip, 3.0f, on ? YELLOW : SKYBLUE);
            }
            if (sel_chain >= 0)
                DrawCircleLinesV(mouse, 6.0f, ColorAlpha(YELLOW, 0.6f));
        }

        // HUD
        const int tx = 14, ts = 14;
        int ty = 14;
        DrawText("2D Skeleton System", tx, ty, 22, RAYWHITE); ty += 32;

        const char* drag_hint = ik_mode ? "Left-drag tip   = IK solve limb"
                                         : "Left-drag joint = rotate bone (FK)";
        DrawText(drag_hint,                       tx, ty, ts, GRAY); ty += 19;
        DrawText("Right-click = deselect",        tx, ty, ts, GRAY); ty += 19;
        DrawText("I = FK/IK   A = animation",      tx, ty, ts, GRAY); ty += 19;
        DrawText("K = skin   N = names   L = limits", tx, ty, ts, GRAY); ty += 19;
        DrawText("R = rest pose",                   tx, ty, ts, GRAY); ty += 19;
        DrawText("F5 = save pose  F9 = load pose",  tx, ty, ts, GRAY); ty += 26;

        DrawText(TextFormat("Mode : %s", ik_mode ? "IK" : "FK"),
                 tx, ty, ts, ik_mode ? SKYBLUE : LIGHTGRAY); ty += 19;

        if (ik_mode) {
            const char* cn = (sel_chain >= 0) ? kChains[sel_chain].name : "(none)";
            DrawText(TextFormat("Chain: %s", cn),
                     tx, ty, ts, sel_chain >= 0 ? YELLOW : LIGHTGRAY); ty += 19;
        } else {
            const char* sn = (sel >= 0) ? sk.bones[sel].name : "(none)";
            DrawText(TextFormat("Bone : %s", sn),
                     tx, ty, ts, sel >= 0 ? YELLOW : LIGHTGRAY); ty += 19;
            if (sel >= 0) {
                const Bone& sb = sk.bones[sel];
                DrawText(TextFormat("local_ang: %.1f deg", sb.local_angle * kR2D),
                         tx, ty, ts, YELLOW); ty += 19;
                if (sb.limited)
                    DrawText(TextFormat("range : [%.0f, %.0f] deg",
                                 (sb.lo - sb.rest_angle) * kR2D,
                                 (sb.hi - sb.rest_angle) * kR2D),
                             tx, ty, ts, ORANGE);
                else
                    DrawText("range : free", tx, ty, ts, LIGHTGRAY);
                ty += 19;
            }
        }

        DrawText(TextFormat("Anim : %s", anim_name(anim)),
                 tx, ty, ts, anim != ANIM_OFF ? GREEN : LIGHTGRAY); ty += 19;
        DrawText(TextFormat("Skin : %s   Limits : %s",
                            show_skin ? "on" : "off", use_limits ? "on" : "off"),
                 tx, ty, ts, LIGHTGRAY);

        if (status_t > 0.0f)
            DrawText(status, tx, GetScreenHeight() - 28, 18,
                     ColorAlpha(WHITE, std::clamp(status_t, 0.0f, 1.0f)));

        DrawFPS(GetScreenWidth() - 90, 12);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

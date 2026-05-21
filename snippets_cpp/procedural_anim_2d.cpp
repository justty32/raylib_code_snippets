// snippets_cpp/procedural_anim_2d.cpp
// 2D skeletal *procedural* animation: actions generated from math (easing +
// springs), not authored keyframes.  Aimed at action-game motion where you
// want rotation + displacement to come out of code (punch, kick, dash, hurt).
//
// ── Techniques on show ───────────────────────────────────────────────────────
// • Spring–damper — every bone's offset and the root position are driven by a
//   critically-ish damped spring chasing a target.  This gives free anticipation
//   lag and overshoot ("secondary motion" / juice) without per-frame keyframing.
// • Easing drives — each action is one or two shaped scalars (windup→strike→
//   settle) built from ease curves; bone rotations are just that scalar times a
//   per-bone coefficient.  Pure math, no pose tables.
// • Additive idle  — a continuous sine "breathing/sway" layer is added on top and
//   smoothly faded OUT while an action plays, then back in (a 1-line blend).
// • Root motion    — actions also push the whole skeleton (lunge, knockback, hop);
//   the root springs back so the demo stays centered (a real game would keep it).
// • Facing flip    — the FK mirrors on X, so the same action code works both ways.
//
// Controls:
//   J        — Punch (front straight)        SPACE — Dash (forward lunge)
//   K        — Kick  (front snap kick)        H     — Hurt (knockback recoil)
//   LEFT/RIGHT — face left / right
//   I        — toggle idle layer     S — toggle skin     TAB — auto-demo on/off
//   R        — reset to rest
//
// Build (Linux, system raylib):
//   g++ snippets_cpp/procedural_anim_2d.cpp -o procedural_anim_2d $(pkg-config --cflags --libs raylib) -std=c++17
//
// Build (Windows MinGW, from repo root):
//   g++ snippets_cpp/procedural_anim_2d.cpp -o procedural_anim_2d.exe ^
//       -Iraylib/src -Lraylib/build_mingw/raylib -lraylib ^
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <vector>

static constexpr float kPi  = 3.14159265f;
static constexpr float kD2R = kPi / 180.0f;
static constexpr float kR2D = 180.0f / kPi;

// ── Easing helpers ──────────────────────────────────────────────────────────

static float clamp01(float x)       { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float ease_out_cubic(float x){ x = clamp01(x); float u = 1 - x; return 1 - u * u * u; }
static float ease_in_cubic(float x) { x = clamp01(x); return x * x * x; }
static float bell(float x)          { return sinf(clamp01(x) * kPi); }  // 0→1→0

// ── Spring–damper (the heart of the "juice") ──────────────────────────────────
//
// Numerically integrate x toward `target` with stiffness k and damping d.
// Sub-stepped so it stays stable at any frame rate.  Slightly under-damped
// values overshoot the target a touch, which is exactly what reads as "alive".

struct Spring {
    float x = 0.0f, v = 0.0f;
    void step(float target, float k, float d, float dt) {
        const float h = 1.0f / 240.0f;
        int n = (int)ceilf(dt / h);
        n = std::clamp(n, 1, 8);
        const float hh = dt / n;
        for (int i = 0; i < n; ++i) {
            const float a = k * (target - x) - d * v;
            v += a * hh;
            x += v * hh;
        }
    }
};

// ── Bone / Skeleton (same FK core as skeleton_2d.cpp, mirror-aware) ────────────

struct Bone {
    int         parent;
    float       rest_angle;   // radians, relative to parent
    float       local_angle;  // set every frame = rest + idle + spring offset
    float       length;
    Color       color;
    int         layer;        // draw order: low = behind, high = in front
    float       skin;         // flesh capsule radius; 0 = none

    Vector2 world_head{}, world_tail{};
    float   world_angle = 0.0f;
};

struct Skeleton {
    std::vector<Bone> bones;
    Vector2           root_pos{};

    int add(int parent, float local_deg, float length, Color col, int layer, float skin) {
        const float r = local_deg * kD2R;
        bones.push_back({ parent, r, r, length, col, layer, skin, {}, {}, 0.0f });
        return (int)bones.size() - 1;
    }

    // facing = +1 faces +x, -1 mirrors the whole rig on X (same angles, flipped).
    void forward_kinematics(float facing) {
        for (auto& b : bones) {
            if (b.parent < 0) {
                b.world_head  = root_pos;
                b.world_angle = b.local_angle;
            } else {
                const Bone& p = bones[b.parent];
                b.world_head  = p.world_tail;
                b.world_angle = p.world_angle + b.local_angle;
            }
            b.world_tail = {
                b.world_head.x + cosf(b.world_angle) * b.length * facing,
                b.world_head.y + sinf(b.world_angle) * b.length
            };
        }
    }
};

// Bone indices — must match build_dummy() insertion order.
enum {
    HUB, SPINE, CHEST, NECK, HEAD,
    FUARM, FFARM, FHAND,   // far (back) arm
    NUARM, NFARM, NHAND,   // near (front) arm
    FTHIGH, FSHIN, FFOOT,  // far (back) leg
    NTHIGH, NSHIN, NFOOT,  // near (front) leg
    BONE_COUNT
};

// A side-facing humanoid standing at `center` (its pelvis).  Y-down:
// -90° = up, 90° = down, 0° = forward(+x).  Far limbs are drawn darker/behind.
static Skeleton build_dummy(Vector2 center) {
    Skeleton sk;
    sk.root_pos = center;

    const Color torso = {  90, 170, 255, 255 };
    const Color head  = { 255, 215,  70, 255 };
    const Color armN  = { 255, 150,  70, 255 }, armF = { 170,  95,  45, 255 };
    const Color legN  = {  90, 220, 120, 255 }, legF = {  55, 140,  80, 255 };

    //              parent   deg  len  color  layer skin
    sk.add(   -1,     0,   0, torso, 1, 0);   // HUB (pelvis, invisible)
    sk.add(  HUB,   -90,  46, torso, 1, 14);  // SPINE  (up)
    sk.add(SPINE,     0,  34, torso, 1, 17);  // CHEST
    sk.add(CHEST,     0,  10, head,  1, 7);   // NECK
    sk.add( NECK,     0,  26, head,  1, 16);  // HEAD

    sk.add(CHEST,   188,  32, armF,  0, 8);   // FUARM (down, slightly back)
    sk.add(FUARM,    -8,  28, armF,  0, 7);   // FFARM
    sk.add(FFARM,    -4,  12, armF,  0, 6);   // FHAND

    sk.add(CHEST,   172,  32, armN,  3, 9);   // NUARM (down, slightly forward)
    sk.add(NUARM,    -8,  28, armN,  3, 8);   // NFARM
    sk.add(NFARM,    -4,  12, armN,  3, 7);   // NHAND

    sk.add(  HUB,    92,  44, legF,  0, 11);  // FTHIGH (down, slightly back)
    sk.add(FTHIGH,   -4,  42, legF,  0, 10);  // FSHIN
    sk.add(FSHIN,   -84,  16, legF,  0, 8);   // FFOOT (forward)

    sk.add(  HUB,    88,  44, legN,  2, 12);  // NTHIGH (down, slightly forward)
    sk.add(NTHIGH,   -4,  42, legN,  2, 11);  // NSHIN
    sk.add(NSHIN,   -84,  16, legN,  2, 9);   // NFOOT

    return sk;
}

// ── Idle layer (continuous, additive) ─────────────────────────────────────────
// Subtle breathing + arm sway, written straight into per-bone offsets (radians).
// Returns an extra vertical "bob" for the root (up = positive).
static float compute_idle(std::vector<float>& idle, float t) {
    std::fill(idle.begin(), idle.end(), 0.0f);
    idle[SPINE] = sinf(t * 1.5f)        * 1.5f * kD2R;
    idle[CHEST] = sinf(t * 1.5f + 0.6f) * 1.0f * kD2R;   // breathe
    idle[HEAD]  = sinf(t * 0.9f)        * 1.5f * kD2R;
    idle[NUARM] = sinf(t * 1.3f)        * 3.0f * kD2R;
    idle[FUARM] = sinf(t * 1.3f + kPi)  * 3.0f * kD2R;
    return sinf(t * 1.5f) * 0.8f;                         // tiny chest-rise bob
}

// ── Procedural actions ─────────────────────────────────────────────────────────
//
// Each action fills additive bone-angle TARGETS (radians) and a root-motion
// target (x = forward in facing space, y = up) for a normalized phase u in [0,1].
// Springs (in main) chase these targets, so anticipation lag and overshoot are
// produced automatically — we only describe the *intended* pose curve here.
//
// Sign convention (offsets add to rest, FK uses world = parent + rest + off):
//   SPINE/CHEST/HEAD : +off leans the torso top toward +x (forward), −off back.
//   N/F arm upper    : −off swings the arm forward/up, +off back/up.
//   N/F thigh        : −off swings the leg forward/up (kick), +off back.

enum Action { ACT_IDLE, ACT_PUNCH, ACT_KICK, ACT_DASH, ACT_HURT, ACT_COUNT };
static const float kDur[ACT_COUNT] = { 0.0f, 0.45f, 0.50f, 0.55f, 0.65f };
static const char* act_name(int a) {
    switch (a) {
        case ACT_PUNCH: return "Punch";
        case ACT_KICK:  return "Kick";
        case ACT_DASH:  return "Dash";
        case ACT_HURT:  return "Hurt";
        default:        return "Idle";
    }
}

// A signed windup→strike→settle driver: −1 (cocked) … +1 (extended) … 0.
static float strike_drive(float u, float wind, float hit) {
    if (u < wind)     return -ease_out_cubic(u / wind);                    // cock to −1
    if (u < hit)      return -1.0f + 2.0f * ease_in_cubic((u - wind) / (hit - wind)); // snap to +1
    return 1.0f - ease_out_cubic((u - hit) / (1.0f - hit));               // settle to 0
}

static void author_action(int act, float u, std::vector<float>& off, Vector2& root) {
    std::fill(off.begin(), off.end(), 0.0f);
    root = { 0.0f, 0.0f };
    const float D = kD2R;

    switch (act) {
        case ACT_PUNCH: {
            const float s   = strike_drive(u, 0.30f, 0.55f);
            const float fwd = std::max(0.0f, s), back = std::max(0.0f, -s);
            off[NUARM] = (-95 * fwd + 18 * back) * D;   // front arm thrusts / cocks
            off[NFARM] = (  8 * fwd - 55 * back) * D;   // elbow extends / curls
            off[FUARM] = ( 22 * fwd -  6 * back) * D;   // back arm counter-swings
            off[FFARM] = (-15 * fwd) * D;
            off[SPINE] = (  6 * fwd -  5 * back) * D;   // lean into it / coil back
            off[CHEST] = (  4 * fwd -  3 * back) * D;
            off[HEAD]  = (  3 * back) * D;
            root.x = 26 * fwd;                          // lunge on the strike
            break;
        }
        case ACT_KICK: {
            const float s   = strike_drive(u, 0.26f, 0.52f);
            const float fwd = std::max(0.0f, s), back = std::max(0.0f, -s);
            off[NTHIGH] = (-72 * fwd + 18 * back) * D;  // raise leg / cock back
            off[NSHIN]  = (-12 * fwd + 48 * back) * D;  // snap shin out / bend knee
            off[NFOOT]  = (-22 * fwd) * D;
            off[SPINE]  = (-9  * fwd) * D;              // lean back to balance
            off[CHEST]  = (-5  * fwd) * D;
            off[NUARM]  = ( 28 * fwd) * D;              // arms windmill for balance
            off[FUARM]  = (-28 * fwd) * D;
            root.x = 8 * fwd;
            break;
        }
        case ACT_DASH: {
            const float a   = bell(u);                                  // 0→1→0 body emphasis
            const float go  = ease_out_cubic(u / 0.30f);                // accelerate out
            const float ret = ease_in_cubic(clamp01((u - 0.45f) / 0.55f)); // pull back in
            off[SPINE]  = ( 12 * a) * D;                                // lean forward
            off[CHEST]  = (  8 * a) * D;
            off[HEAD]   = ( -7 * a) * D;                                // keep head level
            off[NTHIGH] = (-22 * a) * D;  off[FTHIGH] = ( 18 * a) * D;  // split stride
            off[NSHIN]  = ( 22 * a) * D;  off[FSHIN]  = (-12 * a) * D;
            off[NUARM]  = (-26 * a) * D;  off[FUARM]  = ( 26 * a) * D;  // arm pump
            root.x = (go - ret) * 72;                                   // shoot out, return
            root.y = bell(u) * 9;                                       // slight hop
            break;
        }
        case ACT_HURT: {
            const float hit = ease_out_cubic(u / 0.12f);                // instant impact
            const float k   = hit * (1.0f - ease_in_cubic(clamp01((u - 0.20f) / 0.80f)));
            off[SPINE]  = (-17 * k) * D;                                // recoil backward
            off[CHEST]  = (-12 * k) * D;
            off[HEAD]   = (-18 * k) * D;
            off[NUARM]  = (-32 * k) * D;  off[FUARM]  = (-22 * k) * D;  // arms fly up/back
            off[NFARM]  = (-24 * k) * D;
            off[NTHIGH] = ( 12 * k) * D;  off[FTHIGH] = ( -8 * k) * D;  // legs stagger
            root.x = -56 * k;                                           // knocked back
            break;
        }
        default: break;
    }
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static Color darken(Color c, float f) {
    return { (unsigned char)(c.r * f), (unsigned char)(c.g * f),
             (unsigned char)(c.b * f), c.a };
}

// Mirror-safe: every bone is drawn from its world head→tail vector, never from a
// raw angle, so the facing flip needs no special casing here.
static void draw_skin(const Skeleton& sk, const std::vector<int>& order) {
    for (int i : order) {
        const Bone& b = sk.bones[i];
        if (b.skin <= 0.0f || b.length < 1.0f) continue;
        const Color c = darken(b.color, 0.42f);
        DrawLineEx(b.world_head, b.world_tail, b.skin * 2.0f, c);
        DrawCircleV(b.world_head, b.skin, c);
        DrawCircleV(b.world_tail, b.skin, c);
    }
}

static void draw_bones(const Skeleton& sk, const std::vector<int>& order) {
    for (int i : order) {
        const Bone& b = sk.bones[i];
        if (b.length < 1.0f) continue;
        const float th = std::clamp(b.length * 0.13f, 3.0f, 7.0f);
        DrawLineEx(b.world_head, b.world_tail, th, b.color);
        DrawCircleV(b.world_head, th * 0.85f, RAYWHITE);
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(960, 700, "2D Procedural Animation — springs + easing");
    SetTargetFPS(60);

    Vector2 base = { GetScreenWidth() * 0.42f, GetScreenHeight() * 0.46f };
    Skeleton sk  = build_dummy(base);

    // Draw order: back-to-front by layer (FK order can't satisfy this directly).
    std::vector<int> order(sk.bones.size());
    for (int i = 0; i < (int)order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return sk.bones[a].layer < sk.bones[b].layer; });

    const int n = (int)sk.bones.size();
    std::vector<Spring> off_spring(n);     // one per bone, chases the action target
    std::vector<float>  idle(n, 0.0f);
    std::vector<float>  target(n, 0.0f);
    Spring rx, ry, blend;                  // root x/y + idle-fade weight

    int   act     = ACT_IDLE;
    float act_t   = 0.0f;
    float facing  = 1.0f;
    bool  use_idle = true, show_skin = true, demo = false;
    float t = 0.0f, demo_gap = 0.6f;
    int   demo_i = 0;

    auto trigger = [&](int a) { act = a; act_t = 0.0f; };

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        t += dt;

        if (IsWindowResized()) base = { GetScreenWidth() * 0.42f, GetScreenHeight() * 0.46f };

        // ── Input ───────────────────────────────────────────────────────────
        if (IsKeyPressed(KEY_J))     trigger(ACT_PUNCH);
        if (IsKeyPressed(KEY_K))     trigger(ACT_KICK);
        if (IsKeyPressed(KEY_SPACE)) trigger(ACT_DASH);
        if (IsKeyPressed(KEY_H))     trigger(ACT_HURT);
        if (IsKeyPressed(KEY_RIGHT)) facing =  1.0f;
        if (IsKeyPressed(KEY_LEFT))  facing = -1.0f;
        if (IsKeyPressed(KEY_I))     use_idle  = !use_idle;
        if (IsKeyPressed(KEY_S))     show_skin = !show_skin;
        if (IsKeyPressed(KEY_TAB))   demo      = !demo;
        if (IsKeyPressed(KEY_R)) {
            act = ACT_IDLE; act_t = 0.0f; facing = 1.0f;
            for (auto& s : off_spring) s = {};
            rx = ry = {};
        }

        // Auto-demo: cycle the actions with a short gap, for hands-off preview.
        if (demo && act == ACT_IDLE) {
            demo_gap -= dt;
            if (demo_gap <= 0.0f) {
                static const int seq[] = { ACT_PUNCH, ACT_KICK, ACT_DASH, ACT_HURT };
                trigger(seq[demo_i % 4]); demo_i++; demo_gap = 0.6f;
            }
        }

        // ── Advance the current action ────────────────────────────────────────
        float u = 0.0f;
        if (act != ACT_IDLE) {
            act_t += dt;
            u = act_t / kDur[act];
            if (u >= 1.0f) { u = 1.0f; act = ACT_IDLE; act_t = 0.0f; }
        }

        // ── Build targets, then let the springs chase them ────────────────────
        Vector2 root_tgt = { 0.0f, 0.0f };
        author_action(act, u, target, root_tgt);

        blend.step(act != ACT_IDLE ? 1.0f : 0.0f, 120.0f, 22.0f, dt);
        const float idle_w = use_idle ? std::clamp(1.0f - blend.x, 0.0f, 1.0f) : 0.0f;

        for (int i = 0; i < n; ++i)
            off_spring[i].step(target[i], 240.0f, 20.0f, dt);   // slight overshoot
        rx.step(root_tgt.x * facing, 200.0f, 19.0f, dt);
        ry.step(root_tgt.y,          200.0f, 19.0f, dt);

        // ── Compose final pose: rest + faded idle + sprung action offset ──────
        const float bob = use_idle ? compute_idle(idle, t) : 0.0f;
        for (int i = 0; i < n; ++i)
            sk.bones[i].local_angle = sk.bones[i].rest_angle + idle[i] * idle_w + off_spring[i].x;

        sk.root_pos = { base.x + rx.x, base.y - (ry.x + bob * idle_w) };
        sk.forward_kinematics(facing);

        // ── Draw ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 22, 22, 32, 255 });

        for (int x = 0; x < GetScreenWidth();  x += 40)
            DrawLine(x, 0, x, GetScreenHeight(), { 38, 38, 52, 70 });
        for (int y = 0; y < GetScreenHeight(); y += 40)
            DrawLine(0, y, GetScreenWidth(), y, { 38, 38, 52, 70 });

        const float gy = base.y + 92.0f;
        DrawLineEx({ 0, gy }, { (float)GetScreenWidth(), gy }, 1.0f, { 70, 70, 100, 160 });
        DrawEllipse((int)(base.x + rx.x), (int)gy + 4, 52, 10, { 0, 0, 0, 70 });

        if (show_skin) draw_skin(sk, order);
        draw_bones(sk, order);

        // Facing arrow above the head.
        {
            const Vector2 hp = sk.bones[HEAD].world_tail;
            const float a = hp.x + 22.0f * facing, b = hp.x;
            DrawLineEx({ b, hp.y - 14 }, { a, hp.y - 14 }, 2.0f, ColorAlpha(SKYBLUE, 0.8f));
            DrawTriangle({ a + 7 * facing, hp.y - 14 }, { a, hp.y - 18 }, { a, hp.y - 10 },
                         ColorAlpha(SKYBLUE, 0.8f));
        }

        // Root-motion vector (how far the displacement spring has pushed us).
        if (fabsf(rx.x) > 1.0f || fabsf(ry.x) > 1.0f)
            DrawLineEx(base, { base.x + rx.x, base.y - ry.x }, 2.0f, ColorAlpha(ORANGE, 0.7f));

        // HUD
        const int tx = 14, ts = 14; int ty = 14;
        DrawText("Procedural Animation", tx, ty, 22, RAYWHITE); ty += 30;
        DrawText("J Punch   K Kick   SPACE Dash   H Hurt", tx, ty, ts, GRAY); ty += 19;
        DrawText("LEFT/RIGHT face   I idle   S skin",      tx, ty, ts, GRAY); ty += 19;
        DrawText("TAB auto-demo   R reset",                tx, ty, ts, GRAY); ty += 26;

        DrawText(TextFormat("Action : %s", act_name(act)),
                 tx, ty, ts, act != ACT_IDLE ? GREEN : LIGHTGRAY); ty += 19;
        DrawText(TextFormat("Facing : %s   Idle : %s   Demo : %s",
                            facing > 0 ? "right" : "left",
                            use_idle ? "on" : "off", demo ? "on" : "off"),
                 tx, ty, ts, LIGHTGRAY);

        // Phase bar (shows the action timeline; springs lag slightly behind it).
        if (act != ACT_IDLE) {
            const float bw = 220.0f, bx = (float)tx, by = (float)GetScreenHeight() - 40;
            DrawRectangleRounded({ bx, by, bw, 12 }, 0.5f, 6, { 50, 50, 64, 255 });
            DrawRectangleRounded({ bx, by, bw * clamp01(u), 12 }, 0.5f, 6, GREEN);
            for (float m : { 0.25f, 0.5f, 0.75f })
                DrawLineEx({ bx + bw * m, by }, { bx + bw * m, by + 12 }, 1.0f, { 30, 30, 40, 255 });
            DrawText(TextFormat("phase %.2f", u), (int)(bx + bw + 10), (int)by - 1, ts, LIGHTGRAY);
        }

        DrawFPS(GetScreenWidth() - 90, 12);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

// projs_cpp/motion_editor/src/main.cpp
// 2D 動作編輯器：在時間軸上設關鍵幀（拖關節擺姿勢），引擎自動內插＋彈簧補出
// 中間動畫。動作存成純文字 clip 檔；編輯器會「熱載入」該檔 —— 你在外面跑 AI
// agent 改檔，編輯器偵測到變動就即時重載。
//
// 這支沿用 procedural_anim_2d.cpp 的引擎（Bone/FK、Spring、緩動、繪製），
// 動作的「目標偏移」改由關鍵幀資料內插而來（即手冊裡的「表示法 B」）。
// clip 檔格式見 repo 根目錄的 MOTION_AUTHORING.md（第 5 節）。
//
// 操作：
//   左鍵拖關節     — 擺姿勢（設定該骨頭角度）
//   左鍵拖骨盆方塊 — 移動根節點位移
//   時間軸點/拖    — 移動播放頭(scrub)；點關鍵幀標記 = 選取該幀
//   SPACE          — 播放 / 暫停
//   ENTER          — 在播放頭位置 插入/覆寫 關鍵幀
//   DELETE         — 刪除最接近播放頭的關鍵幀
//   , / .          — 跳到上一個 / 下一個關鍵幀
//   E              — 切換選取幀「到下一幀」的緩動曲線
//   - / =          — 縮短 / 加長 clip 總長(duration)
//   L  V  K        — 切換 loop / 彈簧預覽 / 肌肉
//   N              — 新建空白 clip（兩端各一個 rest 幀）
//   F5 / Ctrl+S    — 存檔     F9 — 從檔案重新載入
//
// 編譯（Linux，系統 raylib，從本資料夾）：
//   g++ src/main.cpp -o motion_editor $(pkg-config --cflags --libs raylib) -std=c++17
// 或用 CMake：cmake -B build && cmake --build build
// 執行：./motion_editor [clip檔路徑]   （預設 clips/sample.clip）

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr float kPi  = 3.14159265f;
static constexpr float kD2R = kPi / 180.0f;
static constexpr float kR2D = 180.0f / kPi;

// ── 緩動 ──────────────────────────────────────────────────────────────────────

static float clamp01(float x)        { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float ease_out_cubic(float x) { x = clamp01(x); float u = 1 - x; return 1 - u * u * u; }
static float ease_in_cubic(float x)  { x = clamp01(x); return x * x * x; }
static float ease_inout_cubic(float x) {
    x = clamp01(x);
    return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2;
}

enum Ease { E_LINEAR, E_IN, E_OUT, E_INOUT, E_COUNT };
static const char* kEaseName[E_COUNT] = { "linear", "ease_in", "ease_out", "ease_in_out" };
static float ease_apply(int e, float w) {
    switch (e) {
        case E_IN:    return ease_in_cubic(w);
        case E_OUT:   return ease_out_cubic(w);
        case E_INOUT: return ease_inout_cubic(w);
        default:      return clamp01(w);  // linear
    }
}
static int ease_from(const std::string& s) {
    for (int i = 0; i < E_COUNT; ++i) if (s == kEaseName[i]) return i;
    return E_LINEAR;
}

// ── 彈簧–阻尼（次級動作 / 手感）────────────────────────────────────────────────

struct Spring {
    float x = 0.0f, v = 0.0f;
    void step(float target, float k, float d, float dt) {
        const float h = 1.0f / 240.0f;
        int n = std::clamp((int)ceilf(dt / h), 1, 8);
        const float hh = dt / n;
        for (int i = 0; i < n; ++i) {
            const float a = k * (target - x) - d * v;
            v += a * hh; x += v * hh;
        }
    }
    void set(float val) { x = val; v = 0.0f; }
};

// ── 骨頭 / 骨架（與 procedural_anim_2d.cpp 相同的 FK 核心）─────────────────────

struct Bone {
    int     parent;
    float   rest_angle;   // 弧度，相對父骨
    float   local_angle;
    float   length;
    Color   color;
    int     layer;
    float   skin;
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
    void forward_kinematics() {
        for (auto& b : bones) {
            if (b.parent < 0) { b.world_head = root_pos; b.world_angle = b.local_angle; }
            else {
                const Bone& p = bones[b.parent];
                b.world_head  = p.world_tail;
                b.world_angle = p.world_angle + b.local_angle;
            }
            b.world_tail = { b.world_head.x + cosf(b.world_angle) * b.length,
                             b.world_head.y + sinf(b.world_angle) * b.length };
        }
    }
};

enum {
    HUB, SPINE, CHEST, NECK, HEAD,
    FUARM, FFARM, FHAND, NUARM, NFARM, NHAND,
    FTHIGH, FSHIN, FFOOT, NTHIGH, NSHIN, NFOOT,
    BONE_COUNT
};
static const char* kBoneName[BONE_COUNT] = {
    "HUB", "SPINE", "CHEST", "NECK", "HEAD",
    "FUARM", "FFARM", "FHAND", "NUARM", "NFARM", "NHAND",
    "FTHIGH", "FSHIN", "FFOOT", "NTHIGH", "NSHIN", "NFOOT"
};
static int bone_from(const std::string& s) {
    for (int i = 0; i < BONE_COUNT; ++i) if (s == kBoneName[i]) return i;
    return -1;
}

static Skeleton build_dummy(Vector2 center) {
    Skeleton sk; sk.root_pos = center;
    const Color torso = {  90, 170, 255, 255 };
    const Color head  = { 255, 215,  70, 255 };
    const Color armN  = { 255, 150,  70, 255 }, armF = { 170,  95,  45, 255 };
    const Color legN  = {  90, 220, 120, 255 }, legF = {  55, 140,  80, 255 };
    sk.add(  -1,    0,   0, torso, 1, 0);
    sk.add( HUB,  -90,  46, torso, 1, 14);
    sk.add(SPINE,   0,  34, torso, 1, 17);
    sk.add(CHEST,   0,  10, head,  1, 7);
    sk.add(NECK,    0,  26, head,  1, 16);
    sk.add(CHEST, 188,  32, armF,  0, 8);
    sk.add(FUARM,  -8,  28, armF,  0, 7);
    sk.add(FFARM,  -4,  12, armF,  0, 6);
    sk.add(CHEST, 172,  32, armN,  3, 9);
    sk.add(NUARM,  -8,  28, armN,  3, 8);
    sk.add(NFARM,  -4,  12, armN,  3, 7);
    sk.add( HUB,   92,  44, legF,  0, 11);
    sk.add(FTHIGH, -4,  42, legF,  0, 10);
    sk.add(FSHIN, -84,  16, legF,  0, 8);
    sk.add( HUB,   88,  44, legN,  2, 12);
    sk.add(NTHIGH, -4,  42, legN,  2, 11);
    sk.add(NSHIN, -84,  16, legN,  2, 9);
    return sk;
}

// ── Clip 資料模型（手冊「表示法 B」）───────────────────────────────────────────

struct Keyframe {
    float              t = 0.0f;             // 正規化 0..1
    std::vector<float> off;                  // 每骨偏移（度），size = BONE_COUNT
    Vector2            root{ 0, 0 };          // x 前、y 上（像素）
    int                ease = E_OUT;          // 到下一幀的緩動
    Keyframe() : off(BONE_COUNT, 0.0f) {}
};

struct Clip {
    std::string           name = "untitled";
    float                 duration = 0.5f;
    bool                  loop = false;
    bool                  facing_relative = true;
    float                 spring_k = 240.0f, spring_d = 20.0f;
    std::vector<Keyframe> keys;

    void sort_keys() {
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
    }
};

// 在相位 u 取樣 clip：填入每骨偏移（度）與根位移。
static void sample_clip(const Clip& c, float u, float off_deg[BONE_COUNT], Vector2& root) {
    for (int i = 0; i < BONE_COUNT; ++i) off_deg[i] = 0.0f;
    root = { 0, 0 };
    if (c.keys.empty()) return;
    if (u <= c.keys.front().t) {
        for (int i = 0; i < BONE_COUNT; ++i) off_deg[i] = c.keys.front().off[i];
        root = c.keys.front().root; return;
    }
    if (u >= c.keys.back().t) {
        for (int i = 0; i < BONE_COUNT; ++i) off_deg[i] = c.keys.back().off[i];
        root = c.keys.back().root; return;
    }
    int i0 = 0;
    for (int i = 0; i < (int)c.keys.size(); ++i) { if (c.keys[i].t <= u) i0 = i; else break; }
    const Keyframe& a = c.keys[i0];
    const Keyframe& b = c.keys[i0 + 1];
    const float span = b.t - a.t;
    const float w = span > 1e-6f ? (u - a.t) / span : 0.0f;
    const float e = ease_apply(a.ease, w);
    for (int i = 0; i < BONE_COUNT; ++i) off_deg[i] = a.off[i] + (b.off[i] - a.off[i]) * e;
    root.x = a.root.x + (b.root.x - a.root.x) * e;
    root.y = a.root.y + (b.root.y - a.root.y) * e;
}

// ── Clip 讀寫（純文字，與 MOTION_AUTHORING.md 第 5 節一致）──────────────────────

static bool load_clip(const std::string& path, Clip& out) {
    std::ifstream f(path);
    if (!f) return false;
    Clip c;
    std::string line;
    try {
        while (std::getline(f, line)) {
            const size_t hash = line.find('#');           // 去除整行 / 行尾註解
            if (hash != std::string::npos) line = line.substr(0, hash);
            std::istringstream ss(line);
            std::string key; if (!(ss >> key)) continue;   // 空行
            if (key == "name") { std::string rest; std::getline(ss, rest);
                size_t s = rest.find_first_not_of(" \t");
                c.name = (s == std::string::npos) ? "untitled" : rest.substr(s); }
            else if (key == "duration") { ss >> c.duration; }
            else if (key == "loop")     { std::string v; ss >> v; c.loop = (v == "true" || v == "1"); }
            else if (key == "facing")   { std::string v; ss >> v; c.facing_relative = (v == "relative"); }
            else if (key == "spring")   { ss >> c.spring_k >> c.spring_d; }
            else if (key == "key")      { Keyframe k; ss >> k.t; std::string e; if (ss >> e) k.ease = ease_from(e);
                                          c.keys.push_back(k); }
            else if (key == "root")     { if (!c.keys.empty()) ss >> c.keys.back().root.x >> c.keys.back().root.y; }
            else {                                          // 骨頭名稱 + 角度
                const int bi = bone_from(key);
                float deg; if (bi >= 0 && !c.keys.empty() && (ss >> deg)) c.keys.back().off[bi] = deg;
            }
        }
    } catch (...) { return false; }
    c.duration = std::max(0.05f, c.duration);
    c.sort_keys();
    out = c;
    return true;
}

static bool save_clip(const std::string& path, const Clip& c) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# motion clip v1\n";
    f << "name " << c.name << "\n";
    f << "duration " << c.duration << "\n";
    f << "facing " << (c.facing_relative ? "relative" : "fixed") << "\n";
    f << "loop " << (c.loop ? "true" : "false") << "\n";
    f << "spring " << c.spring_k << " " << c.spring_d << "\n\n";
    for (const auto& k : c.keys) {
        f << "key " << k.t << " " << kEaseName[std::clamp(k.ease, 0, E_COUNT - 1)] << "\n";
        if (fabsf(k.root.x) > 1e-4f || fabsf(k.root.y) > 1e-4f)
            f << "  root " << k.root.x << " " << k.root.y << "\n";
        for (int i = 0; i < BONE_COUNT; ++i)
            if (fabsf(k.off[i]) > 1e-4f) f << "  " << kBoneName[i] << " " << k.off[i] << "\n";
    }
    return (bool)f;
}

static Clip default_clip() {                    // 兩端各一個 rest 幀
    Clip c; c.name = "untitled"; c.duration = 0.5f;
    Keyframe a; a.t = 0.0f; a.ease = E_OUT;
    Keyframe b; b.t = 1.0f; b.ease = E_LINEAR;
    c.keys = { a, b };
    return c;
}

// ── 繪製 ───────────────────────────────────────────────────────────────────────

static Color darken(Color c, float f) {
    return { (unsigned char)(c.r * f), (unsigned char)(c.g * f), (unsigned char)(c.b * f), c.a };
}
static void draw_skin(const Skeleton& sk, const std::vector<int>& order) {
    for (int i : order) {
        const Bone& b = sk.bones[i];
        if (b.skin <= 0.0f || b.length < 1.0f) continue;
        const Color c = darken(b.color, 0.42f);
        DrawLineEx(b.world_head, b.world_tail, b.skin * 2.0f, c);
        DrawCircleV(b.world_head, b.skin, c); DrawCircleV(b.world_tail, b.skin, c);
    }
}
static void draw_bones(const Skeleton& sk, const std::vector<int>& order, int sel) {
    for (int i : order) {
        const Bone& b = sk.bones[i];
        if (b.length < 1.0f) continue;
        const float th = std::clamp(b.length * 0.13f, 3.0f, 7.0f);
        DrawLineEx(b.world_head, b.world_tail, th, b.color);
        const bool on = (i == sel);
        if (on) DrawCircleV(b.world_head, th + 4.0f, ColorAlpha(YELLOW, 0.35f));
        DrawCircleV(b.world_head, th * 0.9f, on ? YELLOW : RAYWHITE);
    }
}
static int pick_bone(const Skeleton& sk, Vector2 m) {
    float best = 16.0f * 16.0f; int hit = -1;
    for (int i = 0; i < (int)sk.bones.size(); ++i) {
        if (sk.bones[i].length < 1.0f) continue;          // 跳過 HUB
        const float dx = m.x - sk.bones[i].world_head.x, dy = m.y - sk.bones[i].world_head.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; hit = i; }
    }
    return hit;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const std::string clip_path = (argc > 1) ? argv[1] : "clips/sample.clip";

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1000, 720, "2D Motion Editor — keyframes + springs (hot-reloads clip)");
    SetTargetFPS(60);

    Vector2 base = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.40f };
    Skeleton sk  = build_dummy(base);

    std::vector<int> order(sk.bones.size());
    for (int i = 0; i < (int)order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return sk.bones[a].layer < sk.bones[b].layer; });

    // 載入 clip（失敗則用預設）。
    Clip clip;
    if (!load_clip(clip_path, clip)) clip = default_clip();
    auto get_mtime = [&]() -> fs::file_time_type {
        std::error_code ec; auto t = fs::last_write_time(clip_path, ec);
        return ec ? fs::file_time_type{} : t;
    };
    fs::file_time_type last_write = get_mtime();
    float reload_timer = 0.0f;

    // 編輯狀態
    float edit_off[BONE_COUNT] = { 0 };       // 目前編輯緩衝（度）
    Vector2 edit_root = { 0, 0 };
    bool  live_edit = false;                  // 顯示編輯緩衝 vs 取樣結果
    float u = 0.0f;                           // 播放頭（相位）
    bool  playing = false;
    int   sel = -1, sel_key = -1;
    bool  show_skin = true, preview_spring = true;
    bool  dirty = false;

    // 拖曳狀態
    bool   bone_drag = false, root_drag = false, scrubbing = false;
    float  drag_local0 = 0, drag_mang0 = 0;
    Vector2 drag_mouse0{}, drag_root0{};

    // 播放用彈簧
    Spring off_spring[BONE_COUNT], rx, ry;

    const char* status = ""; float status_t = 0.0f;
    auto set_status = [&](const char* s) { status = s; status_t = 2.5f; };

    // 取目前「顯示中」的姿勢（暫停時用，給播放/插幀做基準）。
    auto current_pose = [&](float off_rad[BONE_COUNT], Vector2& root) {
        if (live_edit) {
            for (int i = 0; i < BONE_COUNT; ++i) off_rad[i] = edit_off[i] * kD2R;
            root = edit_root;
        } else {
            float deg[BONE_COUNT]; sample_clip(clip, u, deg, root);
            for (int i = 0; i < BONE_COUNT; ++i) off_rad[i] = deg[i] * kD2R;
        }
    };

    auto upsert_key = [&]() {
        Keyframe k; k.t = u; k.root = edit_root; k.ease = E_OUT;
        for (int i = 0; i < BONE_COUNT; ++i) k.off[i] = edit_off[i];
        int found = -1;
        for (int i = 0; i < (int)clip.keys.size(); ++i)
            if (fabsf(clip.keys[i].t - u) < 0.01f) { found = i; break; }
        if (found >= 0) { k.ease = clip.keys[found].ease; clip.keys[found] = k; }
        else            { clip.keys.push_back(k); }
        clip.sort_keys();
        sel_key = -1;
        for (int i = 0; i < (int)clip.keys.size(); ++i)
            if (fabsf(clip.keys[i].t - u) < 1e-4f) { sel_key = i; break; }
        dirty = true;
        set_status(found >= 0 ? "Key overwritten" : "Key inserted");
    };

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        if (IsWindowResized()) { base = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.40f }; }

        // ── 熱載入：每 0.4s 檢查 clip 檔是否被外部（AI agent）改動 ──────────────
        reload_timer += dt;
        if (reload_timer > 0.4f) {
            reload_timer = 0.0f;
            const auto mt = get_mtime();
            if (mt != fs::file_time_type{} && mt != last_write) {
                Clip tmp;
                if (load_clip(clip_path, tmp)) {
                    clip = tmp; last_write = mt; live_edit = false; sel_key = -1;
                    set_status("Clip hot-reloaded");
                }
            }
        }

        // ── 時間軸幾何 ──────────────────────────────────────────────────────────
        const float tlx = 50.0f, tlw = GetScreenWidth() - 100.0f;
        const float tly = GetScreenHeight() - 56.0f, tlh = 14.0f;
        const Vector2 mouse = GetMousePosition();
        const bool in_timeline = mouse.y > tly - 22 && mouse.y < tly + tlh + 10 &&
                                 mouse.x > tlx - 10 && mouse.x < tlx + tlw + 10;
        auto u_at = [&](float mx) { return clamp01((mx - tlx) / tlw); };

        // ── 鍵盤 ────────────────────────────────────────────────────────────────
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (IsKeyPressed(KEY_SPACE)) {
            playing = !playing;
            if (playing) {                       // 播放開始：用目前姿勢種子化彈簧，避免跳動
                float o[BONE_COUNT]; Vector2 r; current_pose(o, r);
                for (int i = 0; i < BONE_COUNT; ++i) off_spring[i].set(o[i]);
                rx.set(r.x); ry.set(r.y);
                live_edit = false;
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { upsert_key(); }
        if (IsKeyPressed(KEY_DELETE) && !clip.keys.empty()) {
            int best = -1; float bd = 0.04f;
            for (int i = 0; i < (int)clip.keys.size(); ++i) {
                const float d = fabsf(clip.keys[i].t - u);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) { clip.keys.erase(clip.keys.begin() + best); sel_key = -1; dirty = true;
                             set_status("Key deleted"); }
        }
        if (IsKeyPressed(KEY_COMMA)) {           // 上一幀
            for (int i = (int)clip.keys.size() - 1; i >= 0; --i)
                if (clip.keys[i].t < u - 1e-3f) { u = clip.keys[i].t; sel_key = i; playing = live_edit = false; break; }
        }
        if (IsKeyPressed(KEY_PERIOD)) {          // 下一幀
            for (int i = 0; i < (int)clip.keys.size(); ++i)
                if (clip.keys[i].t > u + 1e-3f) { u = clip.keys[i].t; sel_key = i; playing = live_edit = false; break; }
        }
        if (IsKeyPressed(KEY_E) && sel_key >= 0) {
            clip.keys[sel_key].ease = (clip.keys[sel_key].ease + 1) % E_COUNT; dirty = true;
        }
        if (IsKeyPressed(KEY_MINUS))  { clip.duration = std::max(0.05f, clip.duration - 0.05f); dirty = true; }
        if (IsKeyPressed(KEY_EQUAL))  { clip.duration += 0.05f; dirty = true; }
        if (IsKeyPressed(KEY_L)) { clip.loop = !clip.loop; dirty = true; }
        if (IsKeyPressed(KEY_V)) preview_spring = !preview_spring;
        if (IsKeyPressed(KEY_K)) show_skin = !show_skin;
        if (IsKeyPressed(KEY_N)) { clip = default_clip(); u = 0; sel_key = -1; live_edit = false; dirty = true;
                                   set_status("New clip"); }
        if (IsKeyPressed(KEY_F5) || (ctrl && IsKeyPressed(KEY_S))) {
            const bool ok = save_clip(clip_path, clip);
            if (ok) { last_write = get_mtime(); dirty = false; }
            set_status(ok ? "Saved" : "Save failed");
        }
        if (IsKeyPressed(KEY_F9)) {
            Clip tmp;
            if (load_clip(clip_path, tmp)) { clip = tmp; last_write = get_mtime(); live_edit = false;
                                             sel_key = -1; dirty = false; set_status("Reloaded from file"); }
            else set_status("Reload failed");
        }

        // ── 滑鼠 ────────────────────────────────────────────────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (in_timeline) {
                int khit = -1; float bd = 8.0f;                 // 先看是否點到關鍵幀標記
                for (int i = 0; i < (int)clip.keys.size(); ++i) {
                    const float kx = tlx + tlw * clip.keys[i].t;
                    if (fabsf(mouse.x - kx) < bd) { bd = fabsf(mouse.x - kx); khit = i; }
                }
                playing = false; live_edit = false;
                if (khit >= 0) { sel_key = khit; u = clip.keys[khit].t; }
                else { scrubbing = true; u = u_at(mouse.x); sel_key = -1; }
            } else {
                // 骨盆方塊（根節點）
                const float dxr = mouse.x - sk.root_pos.x, dyr = mouse.y - sk.root_pos.y;
                if (dxr * dxr + dyr * dyr < 12 * 12) {
                    root_drag = true; drag_mouse0 = mouse; drag_root0 = edit_root;
                    playing = false; live_edit = true;
                } else {
                    const int b = pick_bone(sk, mouse);
                    if (b >= 0) {
                        sel = b; bone_drag = true; playing = false; live_edit = true;
                        drag_local0 = sk.bones[b].local_angle;
                        drag_mang0  = atan2f(mouse.y - sk.bones[b].world_head.y,
                                             mouse.x - sk.bones[b].world_head.x);
                    }
                }
            }
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (scrubbing) { u = u_at(mouse.x); }
            else if (root_drag) {
                edit_root.x = drag_root0.x + (mouse.x - drag_mouse0.x);
                edit_root.y = drag_root0.y - (mouse.y - drag_mouse0.y);   // y 上為正
            } else if (bone_drag && sel >= 0) {
                const Vector2 h = sk.bones[sel].world_head;
                const float cur = atan2f(mouse.y - h.y, mouse.x - h.x);
                const float local = drag_local0 + (cur - drag_mang0);
                edit_off[sel] = (local - sk.bones[sel].rest_angle) * kR2D;
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { scrubbing = bone_drag = root_drag = false; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) sel = -1;

        // ── 推進 / 取樣，組出顯示姿勢 ──────────────────────────────────────────
        float pose_rad[BONE_COUNT]; Vector2 disp_root;
        if (playing) {
            u += dt / clip.duration;
            if (u >= 1.0f) { if (clip.loop) u -= floorf(u); else { u = 1.0f; playing = false; } }
            float tgt[BONE_COUNT]; Vector2 troot; sample_clip(clip, u, tgt, troot);
            if (preview_spring && clip.spring_k > 0.0f) {
                for (int i = 0; i < BONE_COUNT; ++i) {
                    off_spring[i].step(tgt[i] * kD2R, clip.spring_k, clip.spring_d, dt);
                    pose_rad[i] = off_spring[i].x;
                }
                rx.step(troot.x, clip.spring_k, clip.spring_d, dt);
                ry.step(troot.y, clip.spring_k, clip.spring_d, dt);
                disp_root = { rx.x, ry.x };
            } else {
                for (int i = 0; i < BONE_COUNT; ++i) pose_rad[i] = tgt[i] * kD2R;
                disp_root = troot;
            }
        } else {
            current_pose(pose_rad, disp_root);
            if (!live_edit) {                       // 把編輯緩衝同步到取樣結果，方便接著拖曳
                for (int i = 0; i < BONE_COUNT; ++i) edit_off[i] = pose_rad[i] * kR2D;
                edit_root = disp_root;
            }
        }

        for (int i = 0; i < BONE_COUNT; ++i) sk.bones[i].local_angle = sk.bones[i].rest_angle + pose_rad[i];
        sk.root_pos = { base.x + disp_root.x, base.y - disp_root.y };
        sk.forward_kinematics();
        if (status_t > 0) status_t -= dt;

        // ── 繪製 ────────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 22, 22, 32, 255 });
        for (int x = 0; x < GetScreenWidth();  x += 40) DrawLine(x, 0, x, GetScreenHeight(), { 38, 38, 52, 70 });
        for (int y = 0; y < GetScreenHeight(); y += 40) DrawLine(0, y, GetScreenWidth(), y, { 38, 38, 52, 70 });

        const float gy = base.y + 96.0f;
        DrawLineEx({ 0, gy }, { (float)GetScreenWidth(), gy }, 1.0f, { 70, 70, 100, 160 });
        DrawEllipse((int)sk.root_pos.x, (int)gy + 4, 52, 10, { 0, 0, 0, 70 });

        if (show_skin) draw_skin(sk, order);
        draw_bones(sk, order, sel);

        // 根節點(骨盆)把手
        DrawRectanglePro({ sk.root_pos.x, sk.root_pos.y, 12, 12 }, { 6, 6 }, 0,
                         ColorAlpha(root_drag ? YELLOW : SKYBLUE, 0.85f));
        if (fabsf(disp_root.x) > 1 || fabsf(disp_root.y) > 1)
            DrawLineEx(base, sk.root_pos, 2.0f, ColorAlpha(ORANGE, 0.6f));

        // ── 時間軸 ──────────────────────────────────────────────────────────────
        DrawRectangleRounded({ tlx, tly, tlw, tlh }, 0.5f, 6, { 48, 48, 62, 255 });
        for (int s = 1; s < 4; ++s) {
            const float mx = tlx + tlw * (s * 0.25f);
            DrawLineEx({ mx, tly }, { mx, tly + tlh }, 1.0f, { 30, 30, 40, 255 });
        }
        for (int i = 0; i < (int)clip.keys.size(); ++i) {     // 關鍵幀菱形標記
            const float kx = tlx + tlw * clip.keys[i].t, ky = tly + tlh * 0.5f;
            const bool on = (i == sel_key);
            const Color kc = on ? YELLOW : RAYWHITE;
            DrawTriangle({ kx, ky - 9 }, { kx - 6, ky }, { kx + 6, ky }, kc);
            DrawTriangle({ kx - 6, ky }, { kx, ky + 9 }, { kx + 6, ky }, kc);
        }
        const float px = tlx + tlw * clamp01(u);              // 播放頭
        DrawLineEx({ px, tly - 14 }, { px, tly + tlh + 6 }, 2.0f, GREEN);

        // ── HUD ─────────────────────────────────────────────────────────────────
        const int tx = 14, ts = 13; int ty = 12;
        DrawText("2D Motion Editor", tx, ty, 22, RAYWHITE); ty += 30;
        DrawText("L-drag joint=pose  drag pelvis=root  timeline=scrub/select key",
                 tx, ty, ts, GRAY); ty += 18;
        DrawText("SPACE play  ENTER set key  DEL del  ,/. step  E ease",
                 tx, ty, ts, GRAY); ty += 18;
        DrawText("-/= duration  L loop  V spring  K skin  N new  F5 save  F9 reload",
                 tx, ty, ts, GRAY); ty += 24;

        DrawText(TextFormat("clip : %s%s", clip.name.c_str(), dirty ? " *" : ""),
                 tx, ty, ts, dirty ? ORANGE : LIGHTGRAY); ty += 18;
        DrawText(TextFormat("file : %s", clip_path.c_str()), tx, ty, ts, DARKGRAY); ty += 18;
        DrawText(TextFormat("time : %.2fs / %.2fs   keys: %d   loop: %s",
                            u * clip.duration, clip.duration, (int)clip.keys.size(),
                            clip.loop ? "on" : "off"),
                 tx, ty, ts, LIGHTGRAY); ty += 18;
        DrawText(TextFormat("phase: %.3f   %s   spring: %s",
                            u, playing ? "PLAYING" : "paused",
                            preview_spring ? "on" : "off"),
                 tx, ty, ts, playing ? GREEN : LIGHTGRAY); ty += 18;
        if (sel_key >= 0)
            DrawText(TextFormat("sel key #%d  t=%.3f  ease->%s",
                                sel_key, clip.keys[sel_key].t, kEaseName[clip.keys[sel_key].ease]),
                     tx, ty, ts, YELLOW);
        else if (sel >= 0)
            DrawText(TextFormat("bone : %s  %.1f deg", kBoneName[sel], edit_off[sel]),
                     tx, ty, ts, YELLOW);

        if (status_t > 0)
            DrawText(status, tx, GetScreenHeight() - 26, 18,
                     ColorAlpha(WHITE, clamp01(status_t)));
        DrawFPS(GetScreenWidth() - 90, 12);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

// snippets_cpp/procedural_anim_2d.cpp
// 2D 骨架「程序式」動畫：動作由數學（緩動曲線 + 彈簧）生成，而非手刻關鍵幀。
// 針對動作遊戲的運動需求設計 —— 你會希望旋轉與位移都從程式碼長出來
//（出拳、踢、衝刺、受擊）。
//
// ── 展示的技術 ───────────────────────────────────────────────────────────────
// • 彈簧–阻尼 — 每根骨頭的角度偏移與根節點位置，都由一個（接近臨界阻尼的）
//   彈簧追向目標。這會自動產生「預備動作的延遲」與「過衝回彈」（次級動作 /
//   手感），不必逐幀手刻。
// • 緩動驅動 — 每個動作就是一兩個被塑形的純量（蓄力→打擊→收勢），用緩動曲線
//   組成；骨頭旋轉量 = 該純量 × 每骨係數。純數學，沒有姿勢表。
// • 疊加 idle — 一層持續的 sine「呼吸 / 搖擺」疊在最上面，動作播放時平滑淡出、
//   結束後再淡回（一行的權重混合）。
// • 根運動  — 動作同時推動整個骨架（前衝、擊退、彈跳）；demo 中根節點會彈回，
//   讓畫面維持置中（正式遊戲則保留位移即可）。
// • 翻面    — FK 在 X 軸鏡射，所以同一份動作碼左右兩面都通用。
//
// 操作：
//   J        — 出拳（正面直拳）          SPACE — 衝刺（向前突進）
//   K        — 踢（正面前踢）            H     — 受擊（被擊退、回彈）
//   LEFT/RIGHT — 面向左 / 右
//   I        — 切換 idle 疊加層    S — 切換肌肉(skin)    TAB — 自動展示開 / 關
//   R        — 回到休息姿勢(rest)
//
// 編譯（Linux，系統 raylib）：
//   g++ snippets_cpp/procedural_anim_2d.cpp -o procedural_anim_2d $(pkg-config --cflags --libs raylib) -std=c++17
//
// 編譯（Windows MinGW，從 repo 根目錄）：
//   g++ snippets_cpp/procedural_anim_2d.cpp -o procedural_anim_2d.exe ^
//       -Iraylib/src -Lraylib/build_mingw/raylib -lraylib ^
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <vector>

static constexpr float kPi  = 3.14159265f;
static constexpr float kD2R = kPi / 180.0f;   // 角度轉弧度
static constexpr float kR2D = 180.0f / kPi;   // 弧度轉角度

// ── 緩動 (easing) 輔助函式 ────────────────────────────────────────────────────

static float clamp01(float x)       { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float ease_out_cubic(float x){ x = clamp01(x); float u = 1 - x; return 1 - u * u * u; }  // 快→慢
static float ease_in_cubic(float x) { x = clamp01(x); return x * x * x; }                       // 慢→快
static float bell(float x)          { return sinf(clamp01(x) * kPi); }  // 0→1→0 鐘形（起伏一次）

// ── 彈簧–阻尼（「手感」的核心）────────────────────────────────────────────────
//
// 用數值積分讓 x 朝 `target` 移動，剛度為 k、阻尼為 d。內部做了次步進(sub-step)
// 所以任何幀率都穩定。把阻尼設得「略低於臨界阻尼」會讓它稍微衝過目標再回彈，
// 這正是讀起來「有生命」的關鍵。

struct Spring {
    float x = 0.0f, v = 0.0f;
    void step(float target, float k, float d, float dt) {
        const float h = 1.0f / 240.0f;        // 固定次步長，確保穩定
        int n = (int)ceilf(dt / h);
        n = std::clamp(n, 1, 8);
        const float hh = dt / n;
        for (int i = 0; i < n; ++i) {
            const float a = k * (target - x) - d * v;   // 彈力 − 阻尼力
            v += a * hh;
            x += v * hh;
        }
    }
};

// ── 骨頭 / 骨架（與 skeleton_2d.cpp 相同的 FK 核心，但支援鏡射）────────────────

struct Bone {
    int         parent;       // 父骨索引；-1 為根
    float       rest_angle;   // 弧度，相對於父骨的休息角度
    float       local_angle;  // 每幀重設 = rest + idle 偏移 + 彈簧偏移
    float       length;
    Color       color;
    int         layer;        // 繪製順序：數字小在後、大在前
    float       skin;         // 肌肉膠囊半徑；0 = 不畫肌肉

    Vector2 world_head{}, world_tail{};   // FK 算出的世界座標（頭 / 尾）
    float   world_angle = 0.0f;           // FK 算出的世界角度
};

struct Skeleton {
    std::vector<Bone> bones;
    Vector2           root_pos{};

    int add(int parent, float local_deg, float length, Color col, int layer, float skin) {
        const float r = local_deg * kD2R;
        bones.push_back({ parent, r, r, length, col, layer, skin, {}, {}, 0.0f });
        return (int)bones.size() - 1;
    }

    // facing = +1 面向 +x；-1 把整個骨架在 X 軸鏡射（角度不變，只翻 x 投影）。
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

// 骨頭索引 —— 必須與 build_dummy() 的加入順序一致。
enum {
    HUB, SPINE, CHEST, NECK, HEAD,
    FUARM, FFARM, FHAND,   // 遠側（後方）手臂
    NUARM, NFARM, NHAND,   // 近側（前方）手臂
    FTHIGH, FSHIN, FFOOT,  // 遠側（後方）腿
    NTHIGH, NSHIN, NFOOT,  // 近側（前方）腿
    BONE_COUNT
};

// 一個側面朝向的人形，站在 `center`（骨盆位置）。Y 向下：
// -90° = 上、90° = 下、0° = 前(+x)。遠側肢體畫得較暗、且在後方。
static Skeleton build_dummy(Vector2 center) {
    Skeleton sk;
    sk.root_pos = center;

    const Color torso = {  90, 170, 255, 255 };
    const Color head  = { 255, 215,  70, 255 };
    const Color armN  = { 255, 150,  70, 255 }, armF = { 170,  95,  45, 255 };
    const Color legN  = {  90, 220, 120, 255 }, legF = {  55, 140,  80, 255 };

    //              父骨     角度  長度  顏色   層  肌肉
    sk.add(   -1,     0,   0, torso, 1, 0);   // HUB（骨盆，不可見）
    sk.add(  HUB,   -90,  46, torso, 1, 14);  // SPINE  脊椎（向上）
    sk.add(SPINE,     0,  34, torso, 1, 17);  // CHEST  胸
    sk.add(CHEST,     0,  10, head,  1, 7);   // NECK   頸
    sk.add( NECK,     0,  26, head,  1, 16);  // HEAD   頭

    sk.add(CHEST,   188,  32, armF,  0, 8);   // FUARM 遠側上臂（向下、略偏後）
    sk.add(FUARM,    -8,  28, armF,  0, 7);   // FFARM 遠側前臂
    sk.add(FFARM,    -4,  12, armF,  0, 6);   // FHAND 遠側手

    sk.add(CHEST,   172,  32, armN,  3, 9);   // NUARM 近側上臂（向下、略偏前）
    sk.add(NUARM,    -8,  28, armN,  3, 8);   // NFARM 近側前臂
    sk.add(NFARM,    -4,  12, armN,  3, 7);   // NHAND 近側手

    sk.add(  HUB,    92,  44, legF,  0, 11);  // FTHIGH 遠側大腿（向下、略偏後）
    sk.add(FTHIGH,   -4,  42, legF,  0, 10);  // FSHIN  遠側小腿
    sk.add(FSHIN,   -84,  16, legF,  0, 8);   // FFOOT  遠側腳（朝前）

    sk.add(  HUB,    88,  44, legN,  2, 12);  // NTHIGH 近側大腿（向下、略偏前）
    sk.add(NTHIGH,   -4,  42, legN,  2, 11);  // NSHIN  近側小腿
    sk.add(NSHIN,   -84,  16, legN,  2, 9);   // NFOOT  近側腳

    return sk;
}

// ── idle 疊加層（持續、可加總）────────────────────────────────────────────────
// 細微的呼吸 + 手臂搖擺，直接寫進每骨偏移（弧度）。
// 回傳一個額外的根節點垂直「起伏」量（向上為正）。
static float compute_idle(std::vector<float>& idle, float t) {
    std::fill(idle.begin(), idle.end(), 0.0f);
    idle[SPINE] = sinf(t * 1.5f)        * 1.5f * kD2R;
    idle[CHEST] = sinf(t * 1.5f + 0.6f) * 1.0f * kD2R;   // 呼吸
    idle[HEAD]  = sinf(t * 0.9f)        * 1.5f * kD2R;
    idle[NUARM] = sinf(t * 1.3f)        * 3.0f * kD2R;
    idle[FUARM] = sinf(t * 1.3f + kPi)  * 3.0f * kD2R;
    return sinf(t * 1.5f) * 0.8f;                         // 胸口微微起伏
}

// ── 程序式動作 ─────────────────────────────────────────────────────────────────
//
// 每個動作會為一個正規化相位 u∈[0,1] 填入「每骨角度偏移目標」（弧度）以及
//「根運動目標」（x = 面向方向的前方、y = 上）。彈簧（在 main 裡）會去追這些
// 目標，所以預備延遲與過衝會自動產生 —— 這裡只描述「想要的姿勢曲線」。
//
// 正負號慣例（偏移加在 rest 上，FK 用 world = 父 + rest + 偏移）：
//   SPINE/CHEST/HEAD : +偏移 讓軀幹上半往 +x（前）傾，−偏移 往後。
//   近/遠側上臂      : −偏移 手臂往前 / 往上揮，+偏移 往後 / 往上。
//   近/遠側大腿      : −偏移 腿往前 / 往上揮（踢），+偏移 往後。

enum Action { ACT_IDLE, ACT_PUNCH, ACT_KICK, ACT_DASH, ACT_HURT, ACT_COUNT };
static const float kDur[ACT_COUNT] = { 0.0f, 0.45f, 0.50f, 0.55f, 0.65f };  // 各動作秒數
static const char* act_name(int a) {
    switch (a) {
        case ACT_PUNCH: return "Punch";
        case ACT_KICK:  return "Kick";
        case ACT_DASH:  return "Dash";
        case ACT_HURT:  return "Hurt";
        default:        return "Idle";
    }
}

// 一個帶正負號的「蓄力→打擊→收勢」驅動量：−1（蓄力）… +1（出招）… 0（收勢）。
static float strike_drive(float u, float wind, float hit) {
    if (u < wind)     return -ease_out_cubic(u / wind);                    // 蓄力到 −1
    if (u < hit)      return -1.0f + 2.0f * ease_in_cubic((u - wind) / (hit - wind)); // 急速到 +1
    return 1.0f - ease_out_cubic((u - hit) / (1.0f - hit));               // 收勢回 0
}

static void author_action(int act, float u, std::vector<float>& off, Vector2& root) {
    std::fill(off.begin(), off.end(), 0.0f);
    root = { 0.0f, 0.0f };
    const float D = kD2R;

    switch (act) {
        case ACT_PUNCH: {
            const float s   = strike_drive(u, 0.30f, 0.55f);
            const float fwd = std::max(0.0f, s), back = std::max(0.0f, -s);
            off[NUARM] = (-95 * fwd + 18 * back) * D;   // 前臂出拳 / 蓄力後拉
            off[NFARM] = (  8 * fwd - 55 * back) * D;   // 手肘伸直 / 蓄力時彎曲
            off[FUARM] = ( 22 * fwd -  6 * back) * D;   // 後臂反向擺動
            off[FFARM] = (-15 * fwd) * D;
            off[SPINE] = (  6 * fwd -  5 * back) * D;   // 順勢前傾 / 蓄力後仰
            off[CHEST] = (  4 * fwd -  3 * back) * D;
            off[HEAD]  = (  3 * back) * D;
            root.x = 26 * fwd;                          // 打擊瞬間向前衝
            break;
        }
        case ACT_KICK: {
            const float s   = strike_drive(u, 0.26f, 0.52f);
            const float fwd = std::max(0.0f, s), back = std::max(0.0f, -s);
            off[NTHIGH] = (-72 * fwd + 18 * back) * D;  // 抬腿 / 蓄力後收
            off[NSHIN]  = (-12 * fwd + 48 * back) * D;  // 小腿彈出 / 蓄力時屈膝
            off[NFOOT]  = (-22 * fwd) * D;
            off[SPINE]  = (-9  * fwd) * D;              // 後仰保持平衡
            off[CHEST]  = (-5  * fwd) * D;
            off[NUARM]  = ( 28 * fwd) * D;              // 手臂揮動平衡
            off[FUARM]  = (-28 * fwd) * D;
            root.x = 8 * fwd;
            break;
        }
        case ACT_DASH: {
            const float a   = bell(u);                                  // 0→1→0 身體強調量
            const float go  = ease_out_cubic(u / 0.30f);                // 向外加速
            const float ret = ease_in_cubic(clamp01((u - 0.45f) / 0.55f)); // 拉回
            off[SPINE]  = ( 12 * a) * D;                                // 前傾
            off[CHEST]  = (  8 * a) * D;
            off[HEAD]   = ( -7 * a) * D;                                // 頭保持水平
            off[NTHIGH] = (-22 * a) * D;  off[FTHIGH] = ( 18 * a) * D;  // 前後分腿（跨步）
            off[NSHIN]  = ( 22 * a) * D;  off[FSHIN]  = (-12 * a) * D;
            off[NUARM]  = (-26 * a) * D;  off[FUARM]  = ( 26 * a) * D;  // 擺臂
            root.x = (go - ret) * 72;                                   // 衝出去、再收回
            root.y = bell(u) * 9;                                       // 輕微彈跳
            break;
        }
        case ACT_HURT: {
            const float hit = ease_out_cubic(u / 0.12f);                // 瞬間被打中
            const float k   = hit * (1.0f - ease_in_cubic(clamp01((u - 0.20f) / 0.80f)));
            off[SPINE]  = (-17 * k) * D;                                // 向後反弓
            off[CHEST]  = (-12 * k) * D;
            off[HEAD]   = (-18 * k) * D;
            off[NUARM]  = (-32 * k) * D;  off[FUARM]  = (-22 * k) * D;  // 手臂往上 / 往後甩
            off[NFARM]  = (-24 * k) * D;
            off[NTHIGH] = ( 12 * k) * D;  off[FTHIGH] = ( -8 * k) * D;  // 腳步踉蹌
            root.x = -56 * k;                                           // 被擊退
            break;
        }
        default: break;
    }
}

// ── 繪製 ───────────────────────────────────────────────────────────────────────

static Color darken(Color c, float f) {   // f∈0..1，乘在 RGB 上使顏色變暗
    return { (unsigned char)(c.r * f), (unsigned char)(c.g * f),
             (unsigned char)(c.b * f), c.a };
}

// 鏡射安全：每根骨頭都用世界座標的 頭→尾 向量來畫，不直接用角度，
// 所以翻面在這裡不需要任何特例處理。
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
        DrawCircleV(b.world_head, th * 0.85f, RAYWHITE);   // 關節點
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(960, 700, "2D Procedural Animation — springs + easing");
    SetTargetFPS(60);

    Vector2 base = { GetScreenWidth() * 0.42f, GetScreenHeight() * 0.46f };
    Skeleton sk  = build_dummy(base);

    // 繪製順序：依 layer 由後往前（FK 的順序無法直接滿足這個前後關係）。
    std::vector<int> order(sk.bones.size());
    for (int i = 0; i < (int)order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return sk.bones[a].layer < sk.bones[b].layer; });

    const int n = (int)sk.bones.size();
    std::vector<Spring> off_spring(n);     // 每骨一個彈簧，去追動作目標
    std::vector<float>  idle(n, 0.0f);
    std::vector<float>  target(n, 0.0f);
    Spring rx, ry, blend;                  // 根節點 x/y + idle 淡入淡出權重

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

        // ── 輸入 ───────────────────────────────────────────────────────────────
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

        // 自動展示：用短間隔輪播各動作，方便不用手動操作就預覽。
        if (demo && act == ACT_IDLE) {
            demo_gap -= dt;
            if (demo_gap <= 0.0f) {
                static const int seq[] = { ACT_PUNCH, ACT_KICK, ACT_DASH, ACT_HURT };
                trigger(seq[demo_i % 4]); demo_i++; demo_gap = 0.6f;
            }
        }

        // ── 推進目前的動作 ───────────────────────────────────────────────────
        float u = 0.0f;
        if (act != ACT_IDLE) {
            act_t += dt;
            u = act_t / kDur[act];
            if (u >= 1.0f) { u = 1.0f; act = ACT_IDLE; act_t = 0.0f; }
        }

        // ── 建立目標，再讓彈簧去追 ─────────────────────────────────────────────
        Vector2 root_tgt = { 0.0f, 0.0f };
        author_action(act, u, target, root_tgt);

        blend.step(act != ACT_IDLE ? 1.0f : 0.0f, 120.0f, 22.0f, dt);
        const float idle_w = use_idle ? std::clamp(1.0f - blend.x, 0.0f, 1.0f) : 0.0f;

        for (int i = 0; i < n; ++i)
            off_spring[i].step(target[i], 240.0f, 20.0f, dt);   // 略帶過衝
        rx.step(root_tgt.x * facing, 200.0f, 19.0f, dt);
        ry.step(root_tgt.y,          200.0f, 19.0f, dt);

        // ── 組出最終姿勢：rest + 淡化後的 idle + 彈簧化的動作偏移 ───────────────
        const float bob = use_idle ? compute_idle(idle, t) : 0.0f;
        for (int i = 0; i < n; ++i)
            sk.bones[i].local_angle = sk.bones[i].rest_angle + idle[i] * idle_w + off_spring[i].x;

        sk.root_pos = { base.x + rx.x, base.y - (ry.x + bob * idle_w) };
        sk.forward_kinematics(facing);

        // ── 繪製 ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 22, 22, 32, 255 });

        for (int x = 0; x < GetScreenWidth();  x += 40)
            DrawLine(x, 0, x, GetScreenHeight(), { 38, 38, 52, 70 });
        for (int y = 0; y < GetScreenHeight(); y += 40)
            DrawLine(0, y, GetScreenWidth(), y, { 38, 38, 52, 70 });

        const float gy = base.y + 92.0f;   // 地面線 + 影子
        DrawLineEx({ 0, gy }, { (float)GetScreenWidth(), gy }, 1.0f, { 70, 70, 100, 160 });
        DrawEllipse((int)(base.x + rx.x), (int)gy + 4, 52, 10, { 0, 0, 0, 70 });

        if (show_skin) draw_skin(sk, order);
        draw_bones(sk, order);

        // 頭頂的面向箭頭。
        {
            const Vector2 hp = sk.bones[HEAD].world_tail;
            const float a = hp.x + 22.0f * facing, b = hp.x;
            DrawLineEx({ b, hp.y - 14 }, { a, hp.y - 14 }, 2.0f, ColorAlpha(SKYBLUE, 0.8f));
            DrawTriangle({ a + 7 * facing, hp.y - 14 }, { a, hp.y - 18 }, { a, hp.y - 10 },
                         ColorAlpha(SKYBLUE, 0.8f));
        }

        // 根運動向量（位移彈簧目前把我們推了多遠）。
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

        // 相位列（顯示動作時間軸；彈簧會略落後於它）。
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

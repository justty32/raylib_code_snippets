// M6: 完整 ImGui 左側參數面板
#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"

#include "editor_state.hpp"
#include "hex_layout.hpp"
#include "render.hpp"
#include "tools.hpp"

#include "mapcore/generation/heightmap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kInitW = 60;
constexpr int kInitH = 40;

// NOISE Shape 下拉選單；index 0 = None
constexpr const char* kShapeUI[] = {
    "None", "Continents", "Pangaea", "Ring Sea",
    "Island", "Archipelago", "Shattered Archipelago",
};
constexpr const char* kShapeApi[] = {
    "", "continents", "pangaea", "ring_sea",
    "island", "archipelago", "shattered_archipelago",
};
static_assert(IM_ARRAYSIZE(kShapeUI) == IM_ARRAYSIZE(kShapeApi));

constexpr const char* kRidgeModeUI[]  = { "plates", "global" };
constexpr const char* kRidgeModeApi[] = { "plates", "global" };

void fill_demo_heightmap(viewer::EditorState& s) {
    const float cx = static_cast<float>(s.width()  - 1) * 0.5f;
    const float cy = static_cast<float>(s.height() - 1) * 0.5f;
    const float rmax = std::sqrt(cx * cx + cy * cy);
    for (int r = 0; r < s.height(); ++r) {
        for (int q = 0; q < s.width(); ++q) {
            const float dx = static_cast<float>(q) - cx;
            const float dy = static_cast<float>(r) - cy;
            const float d  = std::sqrt(dx * dx + dy * dy) / rmax;
            s.set_h(q, r, std::clamp(1.0f - d, 0.0f, 1.0f));
        }
    }
}

const char* tool_name(viewer::Tool t) {
    switch (t) {
        case viewer::Tool::Raise:       return "Raise";
        case viewer::Tool::Lower:       return "Lower";
        case viewer::Tool::Ridge:       return "Ridge";
        case viewer::Tool::Rift:        return "Rift";
        case viewer::Tool::WaterSource: return "Water Source";
    }
    return "?";
}

// ── 平滑隨機 rate（對齊 app.py:_effective_rate） ────────────────────────
struct RateSmoother {
    float  prev        = 0.0f;
    float  target      = 0.0f;
    double phase_start = 0.0;
    double phase_dur   = 1.5;

    void reseed(float lo, float hi, viewer::ToolRng& rng, double now) {
        prev = target = (hi <= lo) ? lo : rng.uniform_float(lo, hi);
        phase_start = now;
        phase_dur   = static_cast<double>(rng.uniform_float(1.0f, 2.0f));
    }

    float effective(const viewer::EditorState& s, viewer::ToolRng& rng, double now) {
        if (!s.brush_rate_rand) return s.brush_rate;
        const float lo = std::min(s.brush_rate_min, s.brush_rate_max);
        const float hi = std::max(s.brush_rate_min, s.brush_rate_max);
        if (hi <= lo) return lo;
        double elapsed = now - phase_start;
        if (elapsed >= phase_dur) {
            prev        = target;
            target      = rng.uniform_float(lo, hi);
            phase_dur   = static_cast<double>(rng.uniform_float(1.0f, 2.0f));
            phase_start = now;
            elapsed     = 0.0;
        }
        const float t = static_cast<float>(std::clamp(elapsed / phase_dur, 0.0, 1.0));
        const float eased = 0.5f * (1.0f - std::cos(t * 3.14159265358979323846f));
        return prev + (target - prev) * eased;
    }
};

// ── 呼叫 mapcore::generation::generate_heightmap 並 blend 進 state ──────
std::string run_generate_noise(viewer::EditorState& s) {
    using namespace mapcore::generation;
    HeightmapParams p;
    p.octaves         = s.noise_octaves;
    p.persistence     = s.noise_persistence;
    p.base_frequency  = s.noise_base_freq;
    p.ridge_weight    = s.noise_ridge_weight;
    p.ridge_mode      = kRidgeModeApi[std::clamp(s.noise_ridge_mode, 0, 1)];
    p.num_plates      = s.noise_num_plates;
    p.shape           = kShapeApi[std::clamp(s.noise_shape, 0, IM_ARRAYSIZE(kShapeApi) - 1)];
    p.shape_strength  = s.noise_shape_strength;
    p.shape_sea_level = s.sea_level;

    std::optional<uint64_t> seed = (s.noise_seed >= 0)
        ? std::optional<uint64_t>{ static_cast<uint64_t>(s.noise_seed) }
        : std::nullopt;

    std::vector<float> new_hm;
    try {
        new_hm = generate_heightmap(s.width(), s.height(), seed, p);
    } catch (const std::exception& e) {
        return std::string("generate failed: ") + e.what();
    }

    const float blend = std::clamp(s.noise_blend, 0.0f, 1.0f);
    auto& cur = s.heightmap();
    if (blend <= 0.0f) {
        cur = std::move(new_hm);
    } else {
        for (size_t i = 0; i < cur.size(); ++i) {
            cur[i] = blend * cur[i] + (1.0f - blend) * new_hm[i];
        }
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "noise generated (seed=%d, shape=%s)",
                  s.noise_seed, kShapeApi[s.noise_shape][0] ? kShapeApi[s.noise_shape] : "none");
    return buf;
}

} // anonymous

int main()
{
    constexpr int kWinW = 1280;
    constexpr int kWinH = 800;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(kWinW, kWinH, "mapcore_cpp viewer — M6");
    SetTargetFPS(60);
    rlImGuiSetup(true);

    viewer::EditorState state(kInitW, kInitH);
    fill_demo_heightmap(state);

    auto center_camera = [](Camera2D& cam, const viewer::EditorState& s) {
        const Vector2 cmax = viewer::canvas_pixel_size(s.width(), s.height(), s.hex_size);
        cam.target = { cmax.x * 0.5f - 30.0f, cmax.y * 0.5f - 30.0f };
        cam.offset = { static_cast<float>(GetScreenWidth()) * 0.5f,
                       static_cast<float>(GetScreenHeight()) * 0.5f };
    };

    Camera2D camera{};
    camera.zoom     = 1.0f;
    camera.rotation = 0.0f;
    center_camera(camera, state);

    double last_brush_t = 0.0;
    viewer::HexCoord last_brush_hex{ -1, -1 };
    bool brush_active = false;
    viewer::ToolRng tool_rng;
    RateSmoother rate_smoother;
    rate_smoother.reseed(state.brush_rate_min, state.brush_rate_max, tool_rng, GetTime());

    char status[160] = "ready.";

    // New Map 暫存欄位
    int new_w = kInitW;
    int new_h = kInitH;

    while (!WindowShouldClose())
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool ui_wants_mouse = io.WantCaptureMouse;
        const bool ui_wants_kb    = io.WantCaptureKeyboard;

        const Vector2 mouse_screen = GetMousePosition();
        const Vector2 mouse_world  = GetScreenToWorld2D(mouse_screen, camera);
        const viewer::HexCoord under_mouse =
            viewer::pixel_to_hex(mouse_world.x, mouse_world.y, state.hex_size);
        const bool in_bounds = state.in_bounds(under_mouse.q, under_mouse.r);

        // ── Pan ────────────────────────────────────────────────────────
        if (!ui_wants_mouse && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !brush_active) {
            const Vector2 d = GetMouseDelta();
            camera.target.x -= d.x / camera.zoom;
            camera.target.y -= d.y / camera.zoom;
        }

        // ── Zoom-to-cursor ─────────────────────────────────────────────
        if (!ui_wants_mouse) {
            const float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                const Vector2 before = GetScreenToWorld2D(mouse_screen, camera);
                const float factor   = (wheel > 0) ? 1.15f : (1.0f / 1.15f);
                camera.zoom = std::clamp(camera.zoom * factor, 0.25f, 6.0f);
                const Vector2 after  = GetScreenToWorld2D(mouse_screen, camera);
                camera.target.x += before.x - after.x;
                camera.target.y += before.y - after.y;
            }
        }

        // ── 右鍵筆刷 ───────────────────────────────────────────────────
        if (!ui_wants_mouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            brush_active   = true;
            last_brush_t   = 0.0;
            last_brush_hex = {-1, -1};
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            brush_active   = false;
            last_brush_hex = {-1, -1};
        }
        if (brush_active && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && in_bounds) {
            const double now = GetTime();
            const float rate = rate_smoother.effective(state, tool_rng, now);
            const double interval = 1.0 / static_cast<double>(std::max(0.1f, rate));
            if (now - last_brush_t >= interval) {
                last_brush_t = now;
                const float strength = state.brush_strength;
                switch (state.current_tool) {
                    case viewer::Tool::Raise:
                        viewer::apply_brush(state, under_mouse.q, under_mouse.r,  strength);
                        break;
                    case viewer::Tool::Lower:
                        viewer::apply_brush(state, under_mouse.q, under_mouse.r, -strength);
                        break;
                    case viewer::Tool::Ridge:
                        viewer::apply_ridge_stamp(state, under_mouse.q, under_mouse.r, tool_rng);
                        break;
                    case viewer::Tool::Rift:
                        viewer::apply_rift_stamp (state, under_mouse.q, under_mouse.r, tool_rng);
                        break;
                    case viewer::Tool::WaterSource:
                        if (!(under_mouse == last_brush_hex)) {
                            viewer::toggle_water_source(state, under_mouse.q, under_mouse.r);
                            last_brush_hex = under_mouse;
                        }
                        break;
                }
            }
        }

        // ── 鍵盤平移 ───────────────────────────────────────────────────
        if (!ui_wants_kb) {
            const float step = std::max(20.0f, state.hex_size * 2.0f) / camera.zoom;
            if (IsKeyDown(KEY_LEFT))  camera.target.x -= step;
            if (IsKeyDown(KEY_RIGHT)) camera.target.x += step;
            if (IsKeyDown(KEY_UP))    camera.target.y -= step;
            if (IsKeyDown(KEY_DOWN))  camera.target.y += step;
        }

        // ── Render ─────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(Color{28, 28, 32, 255});

        BeginMode2D(camera);
        viewer::draw_world(state, camera);
        if (in_bounds) {
            const int radius = std::max(1, state.brush_size);
            const auto disk = viewer::hex_disk(under_mouse.q, under_mouse.r, radius - 1);
            for (const auto& h : disk) {
                if (!state.in_bounds(h.q, h.r)) continue;
                const Vector2 c = viewer::hex_to_pixel(h.q, h.r, state.hex_size);
                DrawPolyLinesEx(c, 6, state.hex_size, -30.0f, 1.2f, Color{255, 220, 80, 110});
            }
            const Vector2 c = viewer::hex_to_pixel(under_mouse.q, under_mouse.r, state.hex_size);
            DrawPolyLinesEx(c, 6, state.hex_size, -30.0f, 2.5f, Color{255, 220, 80, 255});
        }
        EndMode2D();

        DrawText("M6: full panel", 16, 16, 20, RAYWHITE);
        DrawFPS(16, kWinH - 28);

        rlImGuiBegin();
        ImGui::SetNextWindowPos ({0, 0}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({280, static_cast<float>(GetScreenHeight())}, ImGuiCond_FirstUseEver);
        ImGui::Begin("World Sculptor", nullptr, ImGuiWindowFlags_NoCollapse);

        // ── TOOLS ──
        if (ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("tools");
            const char* tools[] = { "Raise", "Lower", "Ridge", "Rift", "Water Source" };
            for (int i = 0; i < IM_ARRAYSIZE(tools); ++i) {
                if (ImGui::RadioButton(tools[i], static_cast<int>(state.current_tool) == i)) {
                    state.current_tool = static_cast<viewer::Tool>(i);
                }
            }
            ImGui::PopID();
        }

        // ── BRUSH ──
        if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("brush");
            ImGui::SliderInt  ("Size",     &state.brush_size,     1, 10);
            ImGui::SliderFloat("Strength", &state.brush_strength, 0.01f, 0.25f, "%.3f");
            ImGui::SliderFloat("Rate/s",   &state.brush_rate,     1.0f, 60.0f, "%.1f");
            if (ImGui::Checkbox("Random Rate", &state.brush_rate_rand)) {
                rate_smoother.reseed(state.brush_rate_min, state.brush_rate_max, tool_rng, GetTime());
            }
            if (state.brush_rate_rand) {
                ImGui::SliderFloat("Rate Min", &state.brush_rate_min, 1.0f, 60.0f, "%.1f");
                ImGui::SliderFloat("Rate Max", &state.brush_rate_max, 1.0f, 60.0f, "%.1f");
            }
            if (state.current_tool == viewer::Tool::Ridge || state.current_tool == viewer::Tool::Rift) {
                ImGui::SeparatorText("Ridge / Rift");
                ImGui::SliderFloat("Falloff",      &state.brush_falloff, 1.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("Chaos",        &state.brush_chaos,   0.0f, 1.0f, "%.2f");
                ImGui::SliderInt  ("Spokes",       &state.brush_spokes,  0, 8);
                ImGui::Checkbox   ("Spokes Random",&state.brush_spokes_rand);
                if (state.brush_spokes_rand) {
                    ImGui::SliderInt("  Min", &state.brush_spokes_min, 0, 8);
                    ImGui::SliderInt("  Max", &state.brush_spokes_max, 0, 8);
                }
                ImGui::Checkbox("Invert Spokes", &state.brush_spokes_invert);
            }
            ImGui::PopID();
        }

        // ── VIEW（只剩 Height）──
        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("view");
            ImGui::SliderFloat("Sea Level", &state.sea_level, 0.0f, 0.8f, "%.2f");
            ImGui::PopID();
        }

        // ── MAP ──
        if (ImGui::CollapsingHeader("Map")) {
            ImGui::PushID("map");
            ImGui::SliderInt("Width",  &new_w, 10, 300);
            ImGui::SliderInt("Height", &new_h, 10, 300);
            if (ImGui::Button("New Map", ImVec2(-FLT_MIN, 0))) {
                state.resize(new_w, new_h);
                center_camera(camera, state);
                std::snprintf(status, sizeof(status), "new map %dx%d", new_w, new_h);
            }
            if (ImGui::Button("Reset Heights", ImVec2(-FLT_MIN, 0))) {
                state.reset_heights(0.5f);
                std::snprintf(status, sizeof(status), "heights reset");
            }
            if (ImGui::Button("Demo Bump", ImVec2(-FLT_MIN, 0))) {
                fill_demo_heightmap(state);
                std::snprintf(status, sizeof(status), "demo heightmap");
            }
            if (ImGui::Button("Clear Water Sources", ImVec2(-FLT_MIN, 0))) {
                state.water_sources.clear();
            }
            ImGui::PopID();
        }

        // ── NOISE（呼叫 mapcore::generation::generate_heightmap）──
        if (ImGui::CollapsingHeader("Noise")) {
            ImGui::PushID("noise");
            ImGui::InputInt("Seed", &state.noise_seed);
            ImGui::SameLine();
            if (ImGui::SmallButton("Rand")) {
                state.noise_seed = tool_rng.uniform_int(0, 999999);
            }
            ImGui::Combo ("Shape",       &state.noise_shape,       kShapeUI,     IM_ARRAYSIZE(kShapeUI));
            ImGui::SliderFloat("Shape Str",   &state.noise_shape_strength, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Ridge",       &state.noise_ridge_weight,   0.0f, 1.0f, "%.2f");
            ImGui::Combo ("Ridge Mode",  &state.noise_ridge_mode,  kRidgeModeUI, IM_ARRAYSIZE(kRidgeModeUI));
            ImGui::SliderInt  ("Plates",      &state.noise_num_plates,     3, 60);
            ImGui::SliderInt  ("Octaves",     &state.noise_octaves,        1, 8);
            ImGui::SliderFloat("Persistence", &state.noise_persistence,    0.1f, 0.9f, "%.2f");
            ImGui::SliderInt  ("Base Freq",   &state.noise_base_freq,      1, 12);
            ImGui::SliderFloat("Blend",       &state.noise_blend,          0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("0=replace  1=keep existing");
            if (ImGui::Button("Generate Noise", ImVec2(-FLT_MIN, 0))) {
                const std::string msg = run_generate_noise(state);
                std::snprintf(status, sizeof(status), "%s", msg.c_str());
            }
            ImGui::PopID();
        }

        // ── DEBUG / STATUS ──
        ImGui::Separator();
        ImGui::Text("Tool: %s", tool_name(state.current_tool));
        ImGui::Text("Zoom: %.2f", camera.zoom);
        ImGui::Text("Hex: (%d, %d) %s", under_mouse.q, under_mouse.r, in_bounds ? "" : "[out]");
        if (in_bounds) ImGui::Text("h = %.3f", state.get_h(under_mouse.q, under_mouse.r));
        ImGui::TextWrapped("Status: %s", status);

        ImGui::End();
        rlImGuiEnd();

        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}

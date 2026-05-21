// Square Grid World Sculptor — 3D 版
// Camera: 中鍵拖曳=旋轉, 左鍵拖曳=平移, 滾輪=縮放, 方向鍵=旋轉, R=重置
// Brush:  右鍵拖曳（同 2D 版）

#include "raylib.h"
#include "raymath.h"
#include "rlImGui.h"
#include "imgui.h"

#include "editor_state.hpp"
#include "grid_layout.hpp"
#include "render.hpp"
#include "render3d.hpp"
#include "tools.hpp"
#include "sim.hpp"

#include "mapcore/generation/heightmap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace {

constexpr int kInitW = 60;
constexpr int kInitH = 40;

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

// ── 軌道攝影機 ─────────────────────────────────────────────────────────────
struct OrbitalCamera {
    Vector3 target   = {30.0f, 0.0f, 20.0f};
    float   yaw      = 225.0f;  // 水平角（度）
    float   pitch    = 40.0f;   // 仰角（度，5~85）
    float   distance = 50.0f;   // 到 target 的距離

    [[nodiscard]] Camera3D to_camera3d() const noexcept {
        const float yr = yaw   * DEG2RAD;
        const float pr = pitch * DEG2RAD;
        const Vector3 offset = {
            distance * cosf(pr) * sinf(yr),
            distance * sinf(pr),
            distance * cosf(pr) * cosf(yr),
        };
        Camera3D cam;
        cam.position   = Vector3Add(target, offset);
        cam.target     = target;
        cam.up         = {0.0f, 1.0f, 0.0f};
        cam.fovy       = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
        return cam;
    }

    void center_on(const viewer::EditorState& s) noexcept {
        target.x = static_cast<float>(s.width()  - 1) * 0.5f;
        target.y = s.height_scale * 0.25f;
        target.z = static_cast<float>(s.height() - 1) * 0.5f;
        distance = static_cast<float>(std::max(s.width(), s.height())) * 0.9f;
        yaw   = 225.0f;
        pitch = 40.0f;
    }
};

// ── 其他輔助 ───────────────────────────────────────────────────────────────
void fill_demo_heightmap(viewer::EditorState& s) {
    const float cx = static_cast<float>(s.width()  - 1) * 0.5f;
    const float cy = static_cast<float>(s.height() - 1) * 0.5f;
    const float rmax = std::sqrt(cx * cx + cy * cy);
    for (int r = 0; r < s.height(); ++r)
        for (int q = 0; q < s.width(); ++q) {
            const float dx = static_cast<float>(q) - cx;
            const float dy = static_cast<float>(r) - cy;
            s.set_h(q, r, std::clamp(1.0f - std::sqrt(dx*dx + dy*dy) / rmax, 0.0f, 1.0f));
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

// ── 平滑隨機 rate ──────────────────────────────────────────────────────────
struct RateSmoother {
    float  prev        = 0.0f;
    float  target      = 0.0f;
    double phase_start = 0.0;
    double phase_dur   = 1.5;

    void reseed(float lo, float hi, viewer::ToolRng& rng, double now) {
        prev = target = (hi <= lo) ? lo : rng.uniform_float(lo, hi);
        phase_start   = now;
        phase_dur     = static_cast<double>(rng.uniform_float(1.0f, 2.0f));
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
        return prev + (target - prev) * (0.5f * (1.0f - cosf(t * 3.14159265f)));
    }
};

std::string run_generate_noise(viewer::EditorState& s) {
    using namespace mapcore::generation;
    HeightmapParams p;
    p.octaves        = s.noise_octaves;
    p.persistence    = s.noise_persistence;
    p.base_frequency = s.noise_base_freq;
    p.ridge_weight   = s.noise_ridge_weight;
    p.ridge_mode     = kRidgeModeApi[std::clamp(s.noise_ridge_mode, 0, 1)];
    p.num_plates     = s.noise_num_plates;
    p.shape          = kShapeApi[std::clamp(s.noise_shape, 0,
                           static_cast<int>(IM_ARRAYSIZE(kShapeApi)) - 1)];
    p.shape_strength  = s.noise_shape_strength;
    p.shape_sea_level = s.sea_level;

    std::optional<uint64_t> seed = (s.noise_seed >= 0)
        ? std::optional<uint64_t>{static_cast<uint64_t>(s.noise_seed)}
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
        for (std::size_t i = 0; i < cur.size(); ++i)
            cur[i] = blend * cur[i] + (1.0f - blend) * new_hm[i];
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf), "noise generated (seed=%d, shape=%s)",
                  s.noise_seed, kShapeApi[s.noise_shape][0] ? kShapeApi[s.noise_shape] : "none");
    return buf;
}

} // anonymous

// ─────────────────────────────────────────────────────────────────────────
int main()
{
    constexpr int kWinW = 1280;
    constexpr int kWinH = 800;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(kWinW, kWinH, "mapcore_cpp — Square Grid World Sculptor 3D");
    SetTargetFPS(60);
    rlImGuiSetup(true);

    viewer::EditorState state(kInitW, kInitH);
    fill_demo_heightmap(state);

    viewer::TerrainMesh terrain_mesh;
    bool mesh_dirty = true;  // 第一幀立即建立

    OrbitalCamera orbital;

    auto center_camera = [](OrbitalCamera& cam, const viewer::EditorState& s) {
        cam.center_on(s);
    };
    center_camera(orbital, state);

    double last_brush_t = 0.0;
    viewer::GridCoord last_brush_grid{-1, -1};
    bool brush_active = false;
    bool sim_dirty    = false;
    viewer::ToolRng tool_rng;
    RateSmoother rate_smoother;
    rate_smoother.reseed(state.brush_rate_min, state.brush_rate_max, tool_rng, GetTime());

    char status[160] = "ready — 中鍵拖曳旋轉 / 左鍵平移 / 滾輪縮放 / R重置";
    int new_w = kInitW;
    int new_h = kInitH;

    while (!WindowShouldClose())
    {
        const ImGuiIO& io       = ImGui::GetIO();
        const bool ui_wants_mouse = io.WantCaptureMouse;
        const bool ui_wants_kb    = io.WantCaptureKeyboard;
        const float dt            = GetFrameTime();

        // ── 從 orbital state 建 Camera3D ──────────────────────────────────
        const Camera3D cam3d = orbital.to_camera3d();

        // ── 3D Ray picking ────────────────────────────────────────────────
        const Vector2 mouse_screen = GetMousePosition();
        viewer::GridCoord under_mouse{-1, -1};
        if (!ui_wants_mouse) {
            Ray ray = GetMouseRay(mouse_screen, cam3d);
            under_mouse = viewer::pick_terrain_grid(state, ray, state.height_scale);
        }
        const bool in_bounds = state.in_bounds(under_mouse.x, under_mouse.y);

        // ── 軌道旋轉：中鍵拖曳 ───────────────────────────────────────────
        if (!ui_wants_mouse && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            const Vector2 d = GetMouseDelta();
            orbital.yaw   += d.x * 0.4f;
            orbital.pitch -= d.y * 0.4f;
            orbital.pitch  = std::clamp(orbital.pitch, 5.0f, 85.0f);
        }

        // ── 平移：左鍵拖曳（無筆刷時）────────────────────────────────────
        if (!ui_wants_mouse && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !brush_active) {
            const Vector2 d = GetMouseDelta();
            const float pan  = orbital.distance * 0.002f;
            const float yr   = orbital.yaw * DEG2RAD;
            // right_xz = {cos(yr), 0, -sin(yr)}, fwd_xz = {-sin(yr), 0, -cos(yr)}
            orbital.target.x -= (d.x * cosf(yr)  - d.y * sinf(yr)) * pan;
            orbital.target.z -= (d.x * -sinf(yr) - d.y * cosf(yr)) * pan;
        }

        // ── 縮放：滾輪 ───────────────────────────────────────────────────
        if (!ui_wants_mouse) {
            const float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                orbital.distance *= (wheel > 0) ? 0.88f : (1.0f / 0.88f);
                orbital.distance  = std::clamp(orbital.distance, 1.0f, 600.0f);
            }
        }

        // ── 鍵盤：方向鍵旋轉、R 重置 ─────────────────────────────────────
        if (!ui_wants_kb) {
            if (IsKeyDown(KEY_LEFT))  orbital.yaw -= 80.0f * dt;
            if (IsKeyDown(KEY_RIGHT)) orbital.yaw += 80.0f * dt;
            if (IsKeyDown(KEY_UP))    orbital.pitch = std::clamp(orbital.pitch + 60.0f * dt, 5.0f, 85.0f);
            if (IsKeyDown(KEY_DOWN))  orbital.pitch = std::clamp(orbital.pitch - 60.0f * dt, 5.0f, 85.0f);
            if (IsKeyPressed(KEY_R))  { center_camera(orbital, state); }
            // WASD 平移
            const float pan = orbital.distance * 0.015f * dt;
            const float yr  = orbital.yaw * DEG2RAD;
            if (IsKeyDown(KEY_A)) { orbital.target.x -= cosf(yr)  * pan; orbital.target.z += sinf(yr) * pan; }
            if (IsKeyDown(KEY_D)) { orbital.target.x += cosf(yr)  * pan; orbital.target.z -= sinf(yr) * pan; }
            if (IsKeyDown(KEY_W)) { orbital.target.x -= sinf(yr)  * pan; orbital.target.z -= cosf(yr) * pan; }
            if (IsKeyDown(KEY_S)) { orbital.target.x += sinf(yr)  * pan; orbital.target.z += cosf(yr) * pan; }
        }

        // ── 右鍵筆刷 ──────────────────────────────────────────────────────
        if (!ui_wants_mouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            brush_active  = true;
            last_brush_t  = 0.0;
            last_brush_grid = {-1, -1};
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            brush_active  = false;
            last_brush_grid = {-1, -1};
        }
        if (brush_active && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && in_bounds) {
            const double now = GetTime();
            const float rate = rate_smoother.effective(state, tool_rng, now);
            const double interval = 1.0 / static_cast<double>(std::max(0.1f, rate));
            if (now - last_brush_t >= interval) {
                last_brush_t = now;
                switch (state.current_tool) {
                    case viewer::Tool::Raise:
                        viewer::apply_brush(state, under_mouse.x, under_mouse.y,  state.brush_strength);
                        mesh_dirty = sim_dirty = true; break;
                    case viewer::Tool::Lower:
                        viewer::apply_brush(state, under_mouse.x, under_mouse.y, -state.brush_strength);
                        mesh_dirty = sim_dirty = true; break;
                    case viewer::Tool::Ridge:
                        viewer::apply_ridge_stamp(state, under_mouse.x, under_mouse.y, tool_rng);
                        mesh_dirty = sim_dirty = true; break;
                    case viewer::Tool::Rift:
                        viewer::apply_rift_stamp(state, under_mouse.x, under_mouse.y, tool_rng);
                        mesh_dirty = sim_dirty = true; break;
                    case viewer::Tool::WaterSource:
                        if (!(under_mouse == last_brush_grid)) {
                            viewer::toggle_water_source(state, under_mouse.x, under_mouse.y);
                            last_brush_grid = under_mouse;
                        }
                        break;
                }
            }
        }

        // ── Mesh 重建 ─────────────────────────────────────────────────────
        if (mesh_dirty) {
            terrain_mesh.build(state, state.height_scale);
            mesh_dirty = false;
        }

        // ── Render ────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});

        BeginMode3D(cam3d);
        terrain_mesh.draw();
        viewer::draw_water_plane(state, state.height_scale);
        if (in_bounds)
            viewer::draw_brush_cursor_3d(state, under_mouse.x, under_mouse.y, state.height_scale);
        EndMode3D();

        // HUD
        DrawText("Square Grid World Sculptor 3D", 16, 16, 18, RAYWHITE);
        DrawFPS(GetScreenWidth() - 80, 16);

        // ── ImGui 面板 ────────────────────────────────────────────────────
        rlImGuiBegin();
        ImGui::SetNextWindowPos ({0, 0}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({280, static_cast<float>(GetScreenHeight())}, ImGuiCond_FirstUseEver);
        ImGui::Begin("World Sculptor", nullptr, ImGuiWindowFlags_NoCollapse);

        // ── TOOLS ──
        if (ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("tools");
            const char* tools[] = { "Raise", "Lower", "Ridge", "Rift", "Water Source" };
            for (int i = 0; i < (int)IM_ARRAYSIZE(tools); ++i) {
                if (ImGui::RadioButton(tools[i], static_cast<int>(state.current_tool) == i))
                    state.current_tool = static_cast<viewer::Tool>(i);
            }
            ImGui::PopID();
        }

        // ── BRUSH ──
        if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("brush");
            ImGui::SliderInt  ("Size",     &state.brush_size,     1, 10);
            ImGui::SliderFloat("Strength", &state.brush_strength, 0.01f, 0.25f, "%.3f");
            ImGui::SliderFloat("Rate/s",   &state.brush_rate,     1.0f, 60.0f,  "%.1f");
            if (ImGui::Checkbox("Random Rate", &state.brush_rate_rand))
                rate_smoother.reseed(state.brush_rate_min, state.brush_rate_max, tool_rng, GetTime());
            if (state.brush_rate_rand) {
                ImGui::SliderFloat("Rate Min", &state.brush_rate_min, 1.0f, 60.0f, "%.1f");
                ImGui::SliderFloat("Rate Max", &state.brush_rate_max, 1.0f, 60.0f, "%.1f");
            }
            if (state.current_tool == viewer::Tool::Ridge || state.current_tool == viewer::Tool::Rift) {
                ImGui::SeparatorText("Ridge / Rift");
                ImGui::SliderFloat("Falloff",       &state.brush_falloff,       1.0f, 4.0f,   "%.2f");
                ImGui::SliderFloat("Chaos",         &state.brush_chaos,         0.0f, 1.0f,   "%.2f");
                ImGui::SliderInt  ("Spokes",        &state.brush_spokes,        0,    8);
                ImGui::Checkbox   ("Spokes Random", &state.brush_spokes_rand);
                if (state.brush_spokes_rand) {
                    ImGui::SliderInt("  Min", &state.brush_spokes_min, 0, 8);
                    ImGui::SliderInt("  Max", &state.brush_spokes_max, 0, 8);
                }
                ImGui::Checkbox("Invert Spokes", &state.brush_spokes_invert);
                ImGui::SliderFloat("Spoke Jitter°", &state.brush_spoke_jitter, 0.0f, 90.0f, "%.1f");
                ImGui::Separator();
                ImGui::SliderFloat("Wheel Angle°", &state.brush_wheel_angle, 0.0f, 360.0f, "%.0f");
                ImGui::Checkbox("Random Wheel", &state.brush_wheel_rand);
                if (state.brush_wheel_rand) {
                    ImGui::SliderFloat("  Wheel Min°", &state.brush_wheel_min, 0.0f, 360.0f, "%.0f");
                    ImGui::SliderFloat("  Wheel Max°", &state.brush_wheel_max, 0.0f, 360.0f, "%.0f");
                }
            }
            ImGui::PopID();
        }

        // ── VIEW ──
        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("view");
            const char* overlays[] = { "Height", "Ocean", "Temperature", "Rainfall" };
            for (int i = 0; i < 4; ++i) {
                if (ImGui::RadioButton(overlays[i], static_cast<int>(state.overlay) == i)) {
                    state.overlay = static_cast<viewer::Overlay>(i);
                    mesh_dirty    = true;
                }
            }
            if (state.overlay != viewer::Overlay::Height && sim_dirty)
                ImGui::TextColored({1.0f, 0.8f, 0.0f, 1.0f}, "Heightmap changed — re-run sim");
            ImGui::Separator();
            ImGui::SliderFloat("Height Scale", &state.height_scale, 1.0f, 60.0f, "%.1f");
            if (ImGui::IsItemEdited()) mesh_dirty = true;
            ImGui::TextDisabled("垂直高度誇張倍數");
            ImGui::PopID();
        }

        // ── HYDROLOGY ──
        if (ImGui::CollapsingHeader("Hydrology")) {
            ImGui::PushID("hydro");
            if (ImGui::SliderFloat("Sea Level", &state.sea_level, 0.0f, 0.8f, "%.2f"))
                mesh_dirty = true;
            if (ImGui::Button("Run Flood Fill", ImVec2(-FLT_MIN, 0))) {
                viewer::run_flood_fill(state);
                state.overlay = viewer::Overlay::Ocean;
                mesh_dirty = true;
                sim_dirty  = false;
                std::snprintf(status, sizeof(status), "flood fill done.");
            }
            ImGui::PopID();
        }

        // ── CLIMATE ──
        if (ImGui::CollapsingHeader("Climate")) {
            ImGui::PushID("climate");
            ImGui::SliderFloat("Sun Angle",   &state.sun_angle,   0.0f,  90.0f,  "%.1f");
            ImGui::TextDisabled("0=equator  90=polar");
            ImGui::SliderFloat("Wind Dir",    &state.wind_dir,    0.0f,  360.0f, "%.0f");
            ImGui::TextDisabled("0=N  90=E  180=S  270=W");
            ImGui::SliderFloat("Evaporation", &state.evaporation, 0.0f,  1.0f,   "%.2f");
            if (ImGui::Button("Run Climate", ImVec2(-FLT_MIN, 0))) {
                viewer::run_flood_fill(state);
                viewer::run_climate(state);
                state.overlay = viewer::Overlay::Temperature;
                mesh_dirty = true;
                sim_dirty  = false;
                std::snprintf(status, sizeof(status), "climate simulation done.");
            }
            ImGui::PopID();
        }

        // ── MAP ──
        if (ImGui::CollapsingHeader("Map")) {
            ImGui::PushID("map");
            ImGui::SliderInt("Width",  &new_w, 10, 300);
            ImGui::SliderInt("Height", &new_h, 10, 300);
            if (ImGui::Button("New Map", ImVec2(-FLT_MIN, 0))) {
                state.resize(new_w, new_h);
                center_camera(orbital, state);
                mesh_dirty = true;
                sim_dirty  = false;
                std::snprintf(status, sizeof(status), "new map %dx%d", new_w, new_h);
            }
            if (ImGui::Button("Reset Heights", ImVec2(-FLT_MIN, 0))) {
                state.reset_heights(0.5f);
                state.clear_sim_results();
                mesh_dirty = true;
                sim_dirty  = false;
                std::snprintf(status, sizeof(status), "heights reset");
            }
            if (ImGui::Button("Demo Bump", ImVec2(-FLT_MIN, 0))) {
                fill_demo_heightmap(state);
                mesh_dirty = sim_dirty = true;
                std::snprintf(status, sizeof(status), "demo heightmap");
            }
            if (ImGui::Button("Clear Water Sources", ImVec2(-FLT_MIN, 0)))
                state.water_sources.clear();
            ImGui::Separator();
            if (ImGui::Button("Export -> JSON", ImVec2(-FLT_MIN, 0))) {
                const std::string msg = viewer::export_world(state);
                std::snprintf(status, sizeof(status), "%s", msg.c_str());
            }
            ImGui::PopID();
        }

        // ── NOISE ──
        if (ImGui::CollapsingHeader("Noise")) {
            ImGui::PushID("noise");
            ImGui::InputInt("Seed", &state.noise_seed);
            ImGui::SameLine();
            if (ImGui::SmallButton("Rand"))
                state.noise_seed = tool_rng.uniform_int(0, 999999);
            ImGui::Combo ("Shape",       &state.noise_shape,      kShapeUI,     (int)IM_ARRAYSIZE(kShapeUI));
            ImGui::SliderFloat("Shape Str",    &state.noise_shape_strength, 0.0f, 1.0f,  "%.2f");
            ImGui::SliderFloat("Ridge",        &state.noise_ridge_weight,   0.0f, 1.0f,  "%.2f");
            ImGui::Combo ("Ridge Mode",  &state.noise_ridge_mode, kRidgeModeUI, (int)IM_ARRAYSIZE(kRidgeModeUI));
            ImGui::SliderInt  ("Plates",       &state.noise_num_plates,     3,    60);
            ImGui::SliderInt  ("Octaves",      &state.noise_octaves,        1,    8);
            ImGui::SliderFloat("Persistence",  &state.noise_persistence,    0.1f, 0.9f,  "%.2f");
            ImGui::SliderInt  ("Base Freq",    &state.noise_base_freq,      1,    12);
            ImGui::SliderFloat("Blend",        &state.noise_blend,          0.0f, 1.0f,  "%.2f");
            ImGui::TextDisabled("0=replace  1=keep existing");
            if (ImGui::Button("Generate Noise", ImVec2(-FLT_MIN, 0))) {
                const std::string msg = run_generate_noise(state);
                mesh_dirty = sim_dirty = true;
                std::snprintf(status, sizeof(status), "%s", msg.c_str());
            }
            ImGui::PopID();
        }

        // ── CAMERA ──
        if (ImGui::CollapsingHeader("Camera")) {
            ImGui::PushID("cam");
            ImGui::Text("Yaw:   %.1f°", orbital.yaw);
            ImGui::Text("Pitch: %.1f°", orbital.pitch);
            ImGui::Text("Dist:  %.1f",  orbital.distance);
            ImGui::Text("Target: (%.1f, %.1f, %.1f)",
                        orbital.target.x, orbital.target.y, orbital.target.z);
            if (ImGui::Button("Reset Camera [R]", ImVec2(-FLT_MIN, 0)))
                center_camera(orbital, state);
            if (ImGui::Button("Top View", ImVec2(-FLT_MIN, 0))) {
                orbital.pitch = 85.0f;
                orbital.yaw   = 0.0f;
            }
            if (ImGui::Button("Side View (North)", ImVec2(-FLT_MIN, 0))) {
                orbital.pitch = 15.0f;
                orbital.yaw   = 180.0f;
            }
            ImGui::PopID();
        }

        // ── DEBUG ──
        ImGui::Separator();
        ImGui::Text("Tool: %s", tool_name(state.current_tool));
        ImGui::Text("Grid: (%d, %d) %s", under_mouse.x, under_mouse.y, in_bounds ? "" : "[out]");
        if (in_bounds) ImGui::Text("h = %.3f", state.get_h(under_mouse.x, under_mouse.y));
        ImGui::TextWrapped("Status: %s", status);

        ImGui::End();
        rlImGuiEnd();

        EndDrawing();
    }

    terrain_mesh.unload();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}

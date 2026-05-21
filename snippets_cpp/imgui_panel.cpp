// snippets_cpp/imgui_panel.cpp
// Dear ImGui side panel: collapsing sections, sliders, combo, radio buttons,
// checkboxes, buttons, and status text.  Behind the panel sits a simple
// hex tilemap so the camera/zoom controls are also exercised.
//
// Dependencies: raylib + Dear ImGui + rlImGui
// Easiest build: copy this file into projs_cpp/world_sculptor_hex/viewer/ and
// add it to that CMakeLists.txt, or build via the pattern below.
//
// CMake snippet (FetchContent for imgui + rlImGui, same as projs_cpp):
//   see projs_cpp/world_sculptor_hex/viewer/CMakeLists.txt
//
// Manual build (after building those libs):
//   g++ snippets_cpp/imgui_panel.cpp imgui/imgui.cpp imgui/imgui_draw.cpp \
//       imgui/imgui_tables.cpp imgui/imgui_widgets.cpp              \
//       rlimgui_src/rlImGui.cpp                                     \
//       -Iraylib/src -Iimgui -Irlimgui_src                         \
//       -Lraylib/build/raylib -lraylib                              \
//       -lopengl32 -lgdi32 -lwinmm -std=c++17

#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// ── Minimal hex tilemap (same math as tilemap_hex.cpp) ──────────────────────
static constexpr int   kCols    = 25;
static constexpr int   kRows    = 18;
static constexpr float kHexSize = 26.0f;
static constexpr float kSqrt3  = 1.7320508075688772f;

struct HexCoord { int q, r; };
static inline bool      hx_ok  (int q, int r)         { return q>=0&&q<kCols&&r>=0&&r<kRows; }
static inline int        hx_idx (int q, int r)         { return r*kCols+q; }
static inline Vector2    hx_pix (int q, int r, float s){
    return { s*kSqrt3*(q+0.5f*(r&1)), s*1.5f*r };
}
static HexCoord hx_from_px(float px, float py, float s) {
    const float lx = px/s, ly = py/s;
    float qf = (kSqrt3/3.0f)*lx - (1.0f/3.0f)*ly;
    float rf  = (2.0f/3.0f)*ly;
    int rx = (int)std::lround(qf), ry_unused = (int)std::lround(-qf-rf), rz = (int)std::lround(rf);
    (void)ry_unused;
    const float dq = std::abs(rx-qf), dr = std::abs(rz-rf);
    if (dq > dr) rx = -((int)std::lround(-qf-rf)) - rz;
    else         rz = -rx - (int)std::lround(-qf-rf);
    const int r2 = rz, q2 = rx + (r2-(r2&1))/2;
    return {q2, r2};
}

static constexpr std::array<Color,5> kPalette = {{
    { 80,160, 60,255}, { 40, 90,190,255}, {200,180,110,255},
    {130,120,110,255}, {240,240,245,255}
}};
static constexpr const char* kTileName[] = {"grass","water","sand","rock","snow"};

static void zoom_cam(Camera2D& cam, Vector2 pivot, float f) {
    const Vector2 b = GetScreenToWorld2D(pivot,cam);
    cam.zoom = std::clamp(cam.zoom*f, 0.1f, 10.0f);
    const Vector2 a = GetScreenToWorld2D(pivot,cam);
    cam.target.x += b.x-a.x; cam.target.y += b.y-a.y;
}

// ── App state controlled by ImGui ─────────────────────────────────────────────
struct AppState {
    // View
    float   hex_size      = kHexSize;
    float   sea_level     = 0.35f;
    bool    show_border   = true;

    // Brush
    int     brush_size    = 1;
    int     selected_type = 0;   // tile palette index to paint

    // Demo controls
    bool    auto_fill     = false;
    int     fill_type     = 0;

    // Status
    char    status[128]   = "ready.";
};

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "ImGui Side Panel Demo");
    SetTargetFPS(60);
    rlImGuiSetup(true);

    std::vector<int> tiles(kCols*kRows, 0);
    AppState st;

    Camera2D camera{};
    camera.zoom   = 1.0f;
    camera.offset = { GetScreenWidth()*0.5f, GetScreenHeight()*0.5f };
    camera.target = { kCols*kHexSize*kSqrt3*0.5f, kRows*kHexSize*1.5f*0.5f };

    while (!WindowShouldClose()) {
        camera.offset = { GetScreenWidth()*0.5f, GetScreenHeight()*0.5f };
        const ImGuiIO& io     = ImGui::GetIO();
        const bool  ui_mouse  = io.WantCaptureMouse;

        const Vector2 ms = GetMousePosition();
        const Vector2 mw = GetScreenToWorld2D(ms, camera);
        const HexCoord hov = hx_from_px(mw.x, mw.y, st.hex_size);

        // Camera controls (skip when ImGui owns mouse)
        if (!ui_mouse) {
            // Pan: left drag
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                const Vector2 d = GetMouseDelta();
                camera.target.x -= d.x/camera.zoom;
                camera.target.y -= d.y/camera.zoom;
            }
            // Zoom: scroll wheel
            const float wh = GetMouseWheelMove();
            if (wh > 0.0f) zoom_cam(camera, ms, 1.15f);
            if (wh < 0.0f) zoom_cam(camera, ms, 1.0f/1.15f);

            // Right-click: paint tile
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && hx_ok(hov.q, hov.r))
                tiles[hx_idx(hov.q, hov.r)] = st.selected_type;
        }

        // ── Draw world ────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 28, 28, 32, 255 });
        BeginMode2D(camera);

        const Vector2 tl = GetScreenToWorld2D({0,0}, camera);
        const Vector2 br = GetScreenToWorld2D(
            {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, camera);
        const float mg = st.hex_size*2.0f;

        for (int r=0; r<kRows; ++r) {
            for (int q=0; q<kCols; ++q) {
                const Vector2 c = hx_pix(q, r, st.hex_size);
                if (c.x < tl.x-mg || c.x > br.x+mg) continue;
                if (c.y < tl.y-mg || c.y > br.y+mg) continue;
                DrawPoly(c, 6, st.hex_size, -30.0f, kPalette[tiles[hx_idx(q,r)]]);
                if (st.show_border)
                    DrawPolyLinesEx(c, 6, st.hex_size, -30.0f, 1.0f, {0,0,0,50});
            }
        }

        if (hx_ok(hov.q, hov.r)) {
            const Vector2 c = hx_pix(hov.q, hov.r, st.hex_size);
            DrawPolyLinesEx(c, 6, st.hex_size, -30.0f, 2.5f, {255,220,80,255});
        }
        EndMode2D();

        // ── ImGui panel ────────────────────────────────────────────────────
        rlImGuiBegin();

        ImGui::SetNextWindowPos ({0, 0}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({270, static_cast<float>(GetScreenHeight())}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoCollapse);

        // ── Section: View ──────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("view");
            ImGui::SliderFloat("Hex Size",  &st.hex_size,    10.0f, 60.0f, "%.1f");
            ImGui::SliderFloat("Sea Level", &st.sea_level,    0.0f,  1.0f, "%.2f");
            ImGui::Checkbox   ("Show Border", &st.show_border);
            ImGui::PopID();
        }

        // ── Section: Brush ─────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("brush");
            ImGui::SliderInt("Brush Size", &st.brush_size, 1, 8);

            // Radio buttons to pick tile type
            ImGui::Text("Tile Type:");
            for (int i = 0; i < (int)kPalette.size(); ++i) {
                ImGui::SameLine();
                if (ImGui::RadioButton(kTileName[i], st.selected_type == i))
                    st.selected_type = i;
            }
            ImGui::PopID();
        }

        // ── Section: Fill ──────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Fill")) {
            ImGui::PushID("fill");
            // Combo box: select fill type
            const char* fill_items[] = { "grass", "water", "sand", "rock", "snow" };
            ImGui::Combo("Fill Type", &st.fill_type, fill_items, IM_ARRAYSIZE(fill_items));

            if (ImGui::Button("Fill All", ImVec2(-FLT_MIN, 0))) {
                std::fill(tiles.begin(), tiles.end(), st.fill_type);
                std::snprintf(st.status, sizeof(st.status), "filled all with %s", kTileName[st.fill_type]);
            }
            if (ImGui::Button("Clear (grass)", ImVec2(-FLT_MIN, 0))) {
                std::fill(tiles.begin(), tiles.end(), 0);
                std::snprintf(st.status, sizeof(st.status), "cleared to grass");
            }
            ImGui::Checkbox("Auto-fill on hover", &st.auto_fill);
            ImGui::PopID();
        }

        // ── Status bar ─────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Zoom: %.2f", camera.zoom);
        ImGui::Text("Hex:  (%d, %d)%s", hov.q, hov.r, hx_ok(hov.q,hov.r) ? "" : " [out]");
        if (hx_ok(hov.q, hov.r))
            ImGui::Text("Type: %s", kTileName[tiles[hx_idx(hov.q, hov.r)]]);
        ImGui::TextWrapped("Status: %s", st.status);

        ImGui::End();
        rlImGuiEnd();

        DrawFPS(12, GetScreenHeight() - 24);
        EndDrawing();

        // Auto-fill when enabled
        if (st.auto_fill && !ui_mouse && hx_ok(hov.q, hov.r))
            tiles[hx_idx(hov.q, hov.r)] = st.fill_type;
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}

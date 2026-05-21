// snippets_cpp/imgui_panel_3d.cpp
// Dear ImGui side panel controlling a 3D hex tilemap + orbital camera.
// Same ImGui patterns as imgui_panel.cpp but with Camera3D, mouse picking,
// and a 3D-specific "Camera" section in the panel.
//
// Dependencies: raylib + Dear ImGui + rlImGui
// Build: see imgui_panel.cpp header comment for dependency setup.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "rlImGui.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// ── Map / rendering parameters ───────────────────────────────────────────────
static constexpr int   kCols    = 18;
static constexpr int   kRows    = 12;
static constexpr float kHexSz   = 0.9f;
static constexpr float kHexH    = 0.18f;
static constexpr float kSqrt3  = 1.7320508075688772f;

static constexpr std::array<Color,5> kPalette = {{
    { 80,160, 60,255}, { 40, 90,190,255}, {200,180,110,255},
    {130,120,110,255}, {240,240,245,255}
}};
static constexpr const char* kTileName[] = {"grass","water","sand","rock","snow"};

// ── Hex helpers (same as tilemap_hex_3d.cpp) ──────────────────────────────
struct HexCoord { int q, r; };
static inline bool     hx_ok(int q,int r){ return q>=0&&q<kCols&&r>=0&&r<kRows; }
static inline int       hx_idx(int q,int r){ return r*kCols+q; }
static inline Vector2  hx_xz(int q,int r,float s){
    return { s*kSqrt3*(q+0.5f*(r&1)), s*1.5f*r };
}
static HexCoord xz_to_hex(float wx,float wz,float s){
    float lx=wx/s,lz=wz/s;
    float qf=(kSqrt3/3.0f)*lx-(1.0f/3.0f)*lz, rf=(2.0f/3.0f)*lz;
    int rx=(int)std::lround(qf),ry=(int)std::lround(-qf-rf),rz=(int)std::lround(rf);
    float dq=std::abs(rx-qf),dr2=std::abs(ry-(-qf-rf)),dr=std::abs(rz-rf);
    if(dq>dr2&&dq>dr) rx=-ry-rz; else if(dr2>dr) ry=-rx-rz; else rz=-rx-ry;
    (void)ry;
    const int r2=rz, q2=rx+(r2-(r2&1))/2;
    return {q2,r2};
}

static void draw_hex_prism(float cx,float cy,float cz,float sz,float h,Color top,Color side){
    constexpr int N=6;
    const float rot=-30.0f*DEG2RAD;
    Vector3 tv[N], bv[N];
    for(int i=0;i<N;++i){
        float a=rot+(float)i*(2*PI/N);
        tv[i]={cx+sz*cosf(a),cy,cz+sz*sinf(a)};
        bv[i]={tv[i].x,cy-h,tv[i].z};
    }
    rlBegin(RL_TRIANGLES);
    rlColor4ub(top.r,top.g,top.b,top.a);
    for(int i=0;i<N;++i){
        rlVertex3f(cx,cy,cz);
        rlVertex3f(tv[i].x,tv[i].y,tv[i].z);
        rlVertex3f(tv[(i+1)%N].x,tv[(i+1)%N].y,tv[(i+1)%N].z);
    }
    rlColor4ub(side.r,side.g,side.b,side.a);
    for(int i=0;i<N;++i){
        int j=(i+1)%N;
        rlVertex3f(tv[i].x,tv[i].y,tv[i].z); rlVertex3f(bv[i].x,bv[i].y,bv[i].z); rlVertex3f(tv[j].x,tv[j].y,tv[j].z);
        rlVertex3f(tv[j].x,tv[j].y,tv[j].z); rlVertex3f(bv[i].x,bv[i].y,bv[i].z); rlVertex3f(bv[j].x,bv[j].y,bv[j].z);
    }
    rlEnd();
}
static void draw_hex_wire(float cx,float cy,float cz,float sz,Color c){
    constexpr int N=6; const float rot=-30.0f*DEG2RAD;
    for(int i=0;i<N;++i){
        float a0=rot+(float)i*(2*PI/N), a1=rot+(float)(i+1)*(2*PI/N);
        DrawLine3D({cx+sz*cosf(a0),cy,cz+sz*sinf(a0)},{cx+sz*cosf(a1),cy,cz+sz*sinf(a1)},c);
    }
}
static inline Color darken(Color c,float f){ return {(unsigned char)(c.r*f),(unsigned char)(c.g*f),(unsigned char)(c.b*f),c.a}; }

// ── Orbital camera ────────────────────────────────────────────────────────────
struct OrbitalCam {
    Vector3 target   = { kCols*kHexSz*kSqrt3*0.5f, 0, kRows*kHexSz*1.5f*0.5f };
    float   yaw      = 225.0f, pitch = 40.0f, distance = 18.0f;

    Camera3D to_cam() const {
        const float yr=yaw*DEG2RAD, pr=pitch*DEG2RAD;
        Camera3D c;
        c.position={target.x+distance*cosf(pr)*sinf(yr),
                    target.y+distance*sinf(pr),
                    target.z+distance*cosf(pr)*cosf(yr)};
        c.target=target; c.up={0,1,0}; c.fovy=45; c.projection=CAMERA_PERSPECTIVE;
        return c;
    }
    void center_on_map() {
        target = { kCols*kHexSz*kSqrt3*0.5f, 0, kRows*kHexSz*1.5f*0.5f };
        distance = static_cast<float>(std::max(kCols,kRows)) * kHexSz * 1.4f;
        yaw=225; pitch=40;
    }
};

static bool ray_vs_plane(Ray r,float py,Vector3& hit){
    float d=r.direction.y;
    if(std::abs(d)<1e-6f) return false;
    float t=(py-r.position.y)/d;
    if(t<0) return false;
    hit={r.position.x+r.direction.x*t,py,r.position.z+r.direction.z*t};
    return true;
}

// ── App state controlled by ImGui ─────────────────────────────────────────────
struct AppState {
    int     selected_type  = 0;
    bool    show_wire      = true;
    bool    show_grid      = true;
    float   hex_height     = kHexH;
    int     fill_type      = 0;
    char    status[128]    = "ready.";
};

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "ImGui 3D Hex Panel");
    SetTargetFPS(60);
    rlImGuiSetup(true);

    std::vector<int> tiles(kCols*kRows, 0);
    AppState  st;
    OrbitalCam orbit;
    orbit.center_on_map();

    while (!WindowShouldClose()) {
        const ImGuiIO& io    = ImGui::GetIO();
        const bool ui_mouse  = io.WantCaptureMouse;
        const Camera3D cam3d = orbit.to_cam();
        const bool shift     = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        // ── Camera controls (skip when ImGui owns mouse) ──────────────────
        if (!ui_mouse) {
            // Orbit: right drag
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                const Vector2 md = GetMouseDelta();
                orbit.yaw   += md.x*0.25f;
                orbit.pitch  = std::clamp(orbit.pitch-md.y*0.25f, 5.0f, 85.0f);
            }
            // Zoom: scroll
            orbit.distance = std::clamp(orbit.distance - GetMouseWheelMove()*1.2f, 3.0f, 60.0f);
            // Pan: Shift + left drag
            if (shift && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                const Vector2 md = GetMouseDelta();
                const float yr = orbit.yaw*DEG2RAD, sp = orbit.distance*0.004f;
                orbit.target.x -= (cosf(yr)*md.x - sinf(yr)*md.y)*sp;
                orbit.target.z -= (sinf(yr)*md.x + cosf(yr)*md.y)*sp;
            }
        }

        // ── Picking + paint ───────────────────────────────────────────────
        HexCoord hov = {-1,-1};
        if (!ui_mouse) {
            const Ray ray = GetScreenToWorldRay(GetMousePosition(), cam3d);
            Vector3 hit{};
            if (ray_vs_plane(ray, 0.0f, hit)) {
                hov = xz_to_hex(hit.x, hit.z, kHexSz);
                if (!hx_ok(hov.q,hov.r)) hov={-1,-1};
            }
            // Left click without Shift: paint tile
            if (!shift && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hx_ok(hov.q,hov.r))
                tiles[hx_idx(hov.q,hov.r)] = st.selected_type;
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({28,28,32,255});
        BeginMode3D(cam3d);

        for(int r=0;r<kRows;++r) for(int q=0;q<kCols;++q) {
            const Vector2 xz  = hx_xz(q,r,kHexSz);
            const Color   top = kPalette[tiles[hx_idx(q,r)]];
            draw_hex_prism(xz.x,0,xz.y, kHexSz-0.04f, st.hex_height, top, darken(top,0.65f));
            if(st.show_wire) draw_hex_wire(xz.x,0.001f,xz.y,kHexSz,{0,0,0,50});
        }
        if(hx_ok(hov.q,hov.r)){
            const Vector2 xz=hx_xz(hov.q,hov.r,kHexSz);
            draw_hex_wire(xz.x,0.002f,xz.y,kHexSz,{255,220,60,255});
        }
        if(st.show_grid) DrawGrid(std::max(kCols,kRows)+4, kHexSz*kSqrt3);

        EndMode3D();

        // ── ImGui panel ────────────────────────────────────────────────────
        rlImGuiBegin();
        ImGui::SetNextWindowPos ({0,0}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({270, static_cast<float>(GetScreenHeight())}, ImGuiCond_FirstUseEver);
        ImGui::Begin("3D Hex Controls", nullptr, ImGuiWindowFlags_NoCollapse);

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("cam");
            ImGui::SliderFloat("Yaw",      &orbit.yaw,      0.0f,   360.0f, "%.1f°");
            ImGui::SliderFloat("Pitch",    &orbit.pitch,    5.0f,   85.0f,  "%.1f°");
            ImGui::SliderFloat("Distance", &orbit.distance, 3.0f,   60.0f,  "%.1f");
            if (ImGui::Button("Reset Camera", ImVec2(-FLT_MIN, 0)))
                orbit.center_on_map();
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("brush");
            ImGui::Text("Tile Type:");
            for (int i=0; i<(int)kPalette.size(); ++i) {
                ImGui::SameLine();
                if (ImGui::RadioButton(kTileName[i], st.selected_type==i))
                    st.selected_type = i;
            }
            ImGui::SliderFloat("Tile Height", &st.hex_height, 0.05f, 1.0f, "%.2f");
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("Fill")) {
            ImGui::PushID("fill");
            const char* fi[] = {"grass","water","sand","rock","snow"};
            ImGui::Combo("Type", &st.fill_type, fi, IM_ARRAYSIZE(fi));
            if (ImGui::Button("Fill All", ImVec2(-FLT_MIN,0))) {
                std::fill(tiles.begin(), tiles.end(), st.fill_type);
                std::snprintf(st.status, sizeof(st.status), "filled with %s", fi[st.fill_type]);
            }
            if (ImGui::Button("Clear (grass)", ImVec2(-FLT_MIN,0))) {
                std::fill(tiles.begin(), tiles.end(), 0);
                std::snprintf(st.status, sizeof(st.status), "cleared");
            }
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("View")) {
            ImGui::PushID("view");
            ImGui::Checkbox("Show Wire",  &st.show_wire);
            ImGui::Checkbox("Show Grid",  &st.show_grid);
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Text("RDrag=orbit  Scroll=zoom");
        ImGui::Text("Shift+LDrag=pan  LClick=paint");
        ImGui::Text("Hover: (%d,%d)%s", hov.q, hov.r, hx_ok(hov.q,hov.r)?"":" [out]");
        if (hx_ok(hov.q,hov.r)) ImGui::Text("Type: %s", kTileName[tiles[hx_idx(hov.q,hov.r)]]);
        ImGui::TextWrapped("Status: %s", st.status);

        ImGui::End();
        rlImGuiEnd();

        DrawFPS(12, GetScreenHeight()-24);
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}

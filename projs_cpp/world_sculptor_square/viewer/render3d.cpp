#include "render3d.hpp"
#include "render.hpp"     // height_color()
#include "grid_layout.hpp" // grid_disk()

#include <algorithm>
#include <cmath>

namespace viewer {

namespace {

// 依 overlay 決定頂點顏色，與 render.cpp 邏輯對應。
Color vertex_color(const EditorState& s, int x, int y) {
    switch (s.overlay) {
        case Overlay::Ocean:
            return s.get_ocean(x, y)
                   ? Color{30, 90, 180, 255}
                   : Color{120, 170, 80, 255};
        case Overlay::Temperature: {
            float t = std::clamp(s.get_temp(x, y), 0.0f, 1.0f);
            return Color{
                static_cast<unsigned char>(std::min(1.0f, t * 2.0f) * 255.0f),
                30,
                static_cast<unsigned char>(std::min(1.0f, (1.0f - t) * 2.0f) * 255.0f),
                255
            };
        }
        case Overlay::Rainfall: {
            float v = std::clamp(s.get_rain(x, y), 0.0f, 1.0f);
            return Color{
                static_cast<unsigned char>(20.0f * (1.0f - v)),
                static_cast<unsigned char>(80.0f + 120.0f * v),
                static_cast<unsigned char>(100.0f + 155.0f * v),
                255
            };
        }
        default:
            return height_color(s.get_h(x, y), s.sea_level);
    }
}

} // anonymous

void TerrainMesh::build(const EditorState& s, float height_scale) {
    const int W = s.width(), H = s.height();
    if (W < 2 || H < 2) return;

    // 每個 quad = 2 個三角形 = 6 個頂點（unindexed，無 65535 限制）
    const int quad_count = (W - 1) * (H - 1);
    const int vert_count = quad_count * 6;

    // 使用 Raylib MemAlloc，讓 UnloadModel 正確釋放
    auto* verts  = static_cast<float*>        (MemAlloc(sizeof(float)         * vert_count * 3));
    auto* colors = static_cast<unsigned char*>(MemAlloc(sizeof(unsigned char) * vert_count * 4));

    int vi = 0;
    auto add_vert = [&](int gx, int gy) {
        const float h = s.get_h(gx, gy);
        verts[vi * 3 + 0] = static_cast<float>(gx);
        verts[vi * 3 + 1] = h * height_scale;
        verts[vi * 3 + 2] = static_cast<float>(gy);
        const Color c = vertex_color(s, gx, gy);
        colors[vi * 4 + 0] = c.r;
        colors[vi * 4 + 1] = c.g;
        colors[vi * 4 + 2] = c.b;
        colors[vi * 4 + 3] = 255;
        ++vi;
    };

    // 從 +Y 俯視為 CCW，法線朝上
    //  v0(x,y) ------ v1(x+1,y)
    //     |   \           |
    //     |     \         |
    //  v3(x,y+1) --- v2(x+1,y+1)
    for (int y = 0; y < H - 1; ++y) {
        for (int x = 0; x < W - 1; ++x) {
            add_vert(x,     y    );  // v0
            add_vert(x,     y + 1);  // v3
            add_vert(x + 1, y    );  // v1
            add_vert(x + 1, y    );  // v1
            add_vert(x,     y + 1);  // v3
            add_vert(x + 1, y + 1);  // v2
        }
    }

    if (loaded_) { UnloadModel(model_); loaded_ = false; }

    Mesh m{};
    m.vertexCount   = vert_count;
    m.triangleCount = vert_count / 3;
    m.vertices      = verts;
    m.colors        = colors;

    UploadMesh(&m, false);
    model_  = LoadModelFromMesh(m);
    loaded_ = true;
}

void TerrainMesh::draw() const {
    if (!loaded_) return;
    // tint=WHITE → colDiffuse=(1,1,1,1)，頂點色直接輸出（無 lighting）
    DrawModel(model_, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
}

void TerrainMesh::unload() {
    if (loaded_) { UnloadModel(model_); loaded_ = false; }
}

void draw_water_plane(const EditorState& s, float height_scale) {
    const float wy = s.sea_level * height_scale + 0.02f;
    const float cx = static_cast<float>(s.width()  - 1) * 0.5f;
    const float cz = static_cast<float>(s.height() - 1) * 0.5f;
    const float pw = static_cast<float>(s.width());
    const float ph = static_cast<float>(s.height());
    BeginBlendMode(BLEND_ALPHA);
    DrawPlane({cx, wy, cz}, {pw, ph}, Color{30, 90, 210, 85});
    EndBlendMode();
}

void draw_brush_cursor_3d(const EditorState& s, int gx, int gy, float height_scale) {
    if (!s.in_bounds(gx, gy)) return;
    const int radius = std::max(1, s.brush_size);
    const auto disk  = grid_disk(gx, gy, radius - 1);

    for (const auto& g : disk) {
        if (!s.in_bounds(g.x, g.y)) continue;
        const float h  = s.get_h(g.x, g.y) * height_scale + 0.05f;
        const float x0 = static_cast<float>(g.x);
        const float x1 = x0 + 1.0f;
        const float z0 = static_cast<float>(g.y);
        const float z1 = z0 + 1.0f;
        const Color dim{255, 220, 80, 100};
        DrawLine3D({x0, h, z0}, {x1, h, z0}, dim);
        DrawLine3D({x1, h, z0}, {x1, h, z1}, dim);
        DrawLine3D({x1, h, z1}, {x0, h, z1}, dim);
        DrawLine3D({x0, h, z1}, {x0, h, z0}, dim);
    }

    // 中心格加亮
    const float hc  = s.get_h(gx, gy) * height_scale + 0.07f;
    const float cx0 = static_cast<float>(gx);
    const float cx1 = cx0 + 1.0f;
    const float cz0 = static_cast<float>(gy);
    const float cz1 = cz0 + 1.0f;
    const Color bright{255, 220, 80, 255};
    DrawLine3D({cx0, hc, cz0}, {cx1, hc, cz0}, bright);
    DrawLine3D({cx1, hc, cz0}, {cx1, hc, cz1}, bright);
    DrawLine3D({cx1, hc, cz1}, {cx0, hc, cz1}, bright);
    DrawLine3D({cx0, hc, cz1}, {cx0, hc, cz0}, bright);
}

GridCoord pick_terrain_grid(const EditorState& s, Ray ray, float height_scale) {
    if (fabsf(ray.direction.y) < 1e-6f) return {-1, -1};

    // Step 1: y=0 平面，快速粗估
    float t = -ray.position.y / ray.direction.y;
    if (t < 0.0f) return {-1, -1};
    float wx = ray.position.x + ray.direction.x * t;
    float wz = ray.position.z + ray.direction.z * t;
    int gx = static_cast<int>(floorf(wx));
    int gy = static_cast<int>(floorf(wz));

    // 夾到有效範圍後，用該格實際高度精修
    const int gx0 = std::clamp(gx, 0, s.width()  - 1);
    const int gy0 = std::clamp(gy, 0, s.height() - 1);
    const float actual_y = s.get_h(gx0, gy0) * height_scale;
    t = (actual_y - ray.position.y) / ray.direction.y;
    if (t < 0.0f) return {-1, -1};
    wx = ray.position.x + ray.direction.x * t;
    wz = ray.position.z + ray.direction.z * t;
    gx = static_cast<int>(floorf(wx));
    gy = static_cast<int>(floorf(wz));

    if (!s.in_bounds(gx, gy)) return {-1, -1};
    return {gx, gy};
}

} // namespace viewer

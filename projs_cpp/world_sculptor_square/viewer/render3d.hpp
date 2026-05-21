#pragma once
#include "raylib.h"
#include "editor_state.hpp"

namespace viewer {

// 3D 地形 Mesh（unindexed triangle soup，避免 unsigned short index 上限問題）
class TerrainMesh {
public:
    TerrainMesh() = default;
    ~TerrainMesh() { unload(); }
    TerrainMesh(const TerrainMesh&)            = delete;
    TerrainMesh& operator=(const TerrainMesh&) = delete;

    // 依 EditorState 高度圖與當前 overlay 重建整個 mesh。
    void build(const EditorState& s, float height_scale);

    void draw()   const;
    void unload();
    [[nodiscard]] bool valid() const noexcept { return loaded_; }

private:
    Model model_{};
    bool  loaded_ = false;
};

// 在海平面高度畫半透明水面（需在 BeginMode3D 內呼叫）。
void draw_water_plane(const EditorState& s, float height_scale);

// 在 3D 地形上方畫筆刷游標輪廓（黃色 lines）。
void draw_brush_cursor_3d(const EditorState& s, int gx, int gy, float height_scale);

// Ray-heightmap 碰撞：先用 y=0 平面得到粗略位置，再用該格實際高度精修一次。
// 若無交點或超出邊界，回傳 {-1, -1}。
[[nodiscard]] GridCoord pick_terrain_grid(const EditorState& s, Ray ray, float height_scale);

} // namespace viewer

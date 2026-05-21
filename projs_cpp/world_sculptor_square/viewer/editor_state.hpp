#pragma once
// 編輯器狀態：對應 mapcore_cpp/viewer/editor_state.hpp 的方格版本。

#include "grid_layout.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace viewer {

enum class Tool {
    Raise,
    Lower,
    Ridge,
    Rift,
    WaterSource,
};

enum class Overlay {
    Height      = 0,
    Ocean       = 1,
    Temperature = 2,
    Rainfall    = 3,
};

class EditorState {
public:
    EditorState(int width, int height);

    [[nodiscard]] int width()  const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t size() const noexcept { return heightmap_.size(); }

    [[nodiscard]] bool in_bounds(int x, int y) const noexcept {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    [[nodiscard]] float get_h(int x, int y) const noexcept {
        return heightmap_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
    }
    void set_h(int x, int y, float h) noexcept {
        heightmap_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = h;
    }
    void add_h(int x, int y, float delta) noexcept {
        auto& v = heightmap_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
        v += delta;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
    }

    [[nodiscard]] std::vector<float>&       heightmap()       noexcept { return heightmap_; }
    [[nodiscard]] const std::vector<float>& heightmap() const noexcept { return heightmap_; }

    void resize(int width, int height);
    void reset_heights(float h = 0.5f);

    // ── 場景參數 ──
    float cell_size    = 16.0f;  // 2D 像素用（grid_layout 相容）
    float sea_level    = 0.35f;
    float height_scale = 10.0f;  // 3D 高度縮放倍數

    // ── 工具狀態 ──
    Tool  current_tool   = Tool::Raise;
    int   brush_size     = 3;
    float brush_strength = 0.05f;
    float brush_rate     = 30.0f;

    // ── Ridge/Rift 徑向 stamp 參數 ──
    float brush_falloff       = 2.0f;
    float brush_chaos         = 0.0f;
    int   brush_spokes        = 0;
    bool  brush_spokes_rand   = false;
    int   brush_spokes_min    = 0;
    int   brush_spokes_max    = 0;
    bool  brush_spokes_invert = false;
    float brush_spoke_jitter  = 0.0f;   // ±jitter per spoke（度）
    float brush_wheel_angle   = 0.0f;   // 輪盤固定旋轉角度（度）
    bool  brush_wheel_rand    = false;  // 是否隨機輪盤角度
    float brush_wheel_min     = 0.0f;   // 隨機輪盤下限（度）
    float brush_wheel_max     = 360.0f; // 隨機輪盤上限（度）

    // ── 平滑隨機 rate ──
    bool  brush_rate_rand     = false;
    float brush_rate_min      = 5.0f;
    float brush_rate_max      = 40.0f;

    // ── View overlay ──
    Overlay overlay = Overlay::Height;

    // ── 氣候模擬參數 ──
    float sun_angle   = 23.5f;  // 黃道傾角（度），控制緯度受熱
    float wind_dir    = 270.0f; // 風向（度）：0=N 90=E 180=S 270=W
    float evaporation = 0.5f;   // 水氣越山損失係數

    // ── Noise 生成參數 ──
    int   noise_seed             = 42;
    int   noise_shape            = 0;
    float noise_shape_strength   = 0.85f;
    float noise_ridge_weight     = 0.0f;
    int   noise_ridge_mode       = 0;
    int   noise_num_plates       = 20;
    int   noise_octaves          = 4;
    float noise_persistence      = 0.5f;
    int   noise_base_freq        = 4;
    float noise_blend            = 0.0f;

    // ── 水源 marker ──
    std::vector<GridCoord> water_sources;

    // ── 模擬結果（Flood Fill + Climate） ──
    std::vector<bool>  ocean_mask;   // flat W*H
    std::vector<float> temperature;  // flat W*H，0=冷 1=熱
    std::vector<float> rainfall;     // flat W*H，0=乾 1=濕

    // 重置模擬結果為預設值（resize 或 new map 後呼叫）
    void clear_sim_results();

    // 便捷取值（避免 render.cpp 重複計算索引）
    [[nodiscard]] bool  get_ocean(int x, int y) const noexcept {
        return ocean_mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x];
    }
    [[nodiscard]] float get_temp(int x, int y) const noexcept {
        return temperature[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x];
    }
    [[nodiscard]] float get_rain(int x, int y) const noexcept {
        return rainfall[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x];
    }

private:
    int width_;
    int height_;
    std::vector<float> heightmap_;
};

} // namespace viewer

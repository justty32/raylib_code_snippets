#pragma once
// 河流：拓撲、流量與生成（RimWorld 風）。
//
// 河流走 hex 邊，每條邊只被「兩個共享 tile 中的一個」儲存：
//   DIRECTIONS[0,1,2] (E/NE/NW) → tile 本身的 Tile.rivers slot 0/1/2
//   DIRECTIONS[3,4,5] (W/SW/SE) → 由鄰居儲存 slot 0/1/2（對應 dir - 3）
//
// rivers 存 3 × 8-bit 流量打包（uint32_t，只用 24 bit）：
//   slot 0 = bits 0-7, slot 1 = bits 8-15, slot 2 = bits 16-23
//
// 外部 API（保留與 Python 版相同命名）：
//   has_river_edge / get_river_strength / set_river_strength / set_river_edge
//   add_river_flow / iter_river_edges
//   generate_rivers
//   classify_river_strength

#include "mapcore/map.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace mapcore {

// 渲染用：direction d 的邊由哪兩個 corner 連起來（pointy-top，corner 從 -30° 順時針）
extern const std::array<std::pair<int,int>, 6> EDGE_CORNERS;

constexpr int  RIVER_BITS         = 8;
constexpr int  RIVER_MASK         = 0xFF;
constexpr int  RIVER_MAX_STRENGTH = 0xFF;

enum class RiverClass : uint8_t {
    CREEK       = 1,  // strength  1 ~ 79
    RIVER       = 2,  // strength 80 ~ 159
    LARGE_RIVER = 3,  // strength 160 ~ 255
};
[[nodiscard]] RiverClass classify_river_strength(int strength) noexcept;

// ── 存儲層 ─────────────────────────────────────────────────────────────────

[[nodiscard]] int  get_river_strength(const TileMap& map, const Hex& h, int direction) noexcept;
void               set_river_strength(TileMap& map, const Hex& h, int direction, int strength) noexcept;
void               add_river_flow    (TileMap& map, const Hex& h, int direction, int amount = 1) noexcept;
[[nodiscard]] bool has_river_edge    (const TileMap& map, const Hex& h, int direction) noexcept;
void               set_river_edge    (TileMap& map, const Hex& h, int direction, bool value = true) noexcept;

// Iterate: callback(origin_hex, direction 0..2, strength)
void iter_river_edges(const TileMap& map,
    std::function<void(const Hex&, int, int)> callback);

// ── 生成 ────────────────────────────────────────────────────────────────────

struct RiverGenParams {
    float rainfall_scale          = 1000.0f;
    float spawn_flow_threshold    = 600.0f;
    float degrade_threshold       = 200.0f;
    float branch_flow_threshold   = 400.0f;
    float branch_chance           = 0.3f;
    float flow_strength_scale     = 0.05f;
    float evaporation_scale       = 1.0f;
    int   min_sea_size            = 1;
    int   min_seed_spacing        = 1;
};

// rainfall[r*W+q]、heightmap[r*W+q]、temperature（可選）均為寬優先扁平陣列。
// 回傳標記的河流邊數量。
int generate_rivers(
    TileMap& tile_map,
    const std::vector<float>& heightmap,
    const std::vector<float>& rainfall,
    const std::vector<float>* temperature = nullptr,  // nullptr → 近似溫度
    std::optional<uint64_t>   seed        = std::nullopt,
    const RiverGenParams&     params      = {}
);

} // namespace mapcore

#pragma once
// 地圖容器：2D array 儲存 Tile 的平行四邊形地圖。
// 對齊 Python 版設計：_rows[r][q] → flat std::vector<Tile> 索引 r*width+q。
// Tile.terrain 儲存 uint16_t id，與 TerrainRegistry 對應。

#include "mapcore/hex.hpp"
#include "mapcore/terrain.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace mapcore {

// 內建地形 id 常數；對應 TerrainType IntEnum
namespace TerrainType {
    constexpr uint16_t OCEAN     = 0;
    constexpr uint16_t COAST     = 1;
    constexpr uint16_t PLAINS    = 2;
    constexpr uint16_t GRASSLAND = 3;
    constexpr uint16_t DESERT    = 4;
    constexpr uint16_t TUNDRA    = 5;
    constexpr uint16_t SNOW      = 6;
    constexpr uint16_t FOREST    = 7;
    constexpr uint16_t HILL      = 8;
    constexpr uint16_t MOUNTAIN  = 9;
    constexpr uint16_t LAKE      = 10;
} // namespace TerrainType

// 地勢起伏 5 級。對齊 RimWorld Hilliness.cs。
enum class Hilliness : uint8_t {
    UNDEFINED  = 0,
    FLAT       = 1,
    SMALL_HILLS= 2,
    LARGE_HILLS= 3,
    MOUNTAINOUS= 4,
    IMPASSABLE = 5,
};

struct Tile {
    uint16_t terrain{TerrainType::PLAINS};
    // rivers：3 × 8-bit 流量打包進 uint32_t（與 Python 版 bit layout 完全一致）
    // bits 0-7  = E (dir 0), bits 8-15 = NE (dir 1), bits 16-23 = NW (dir 2)
    uint32_t rivers{0};
    Hilliness hilliness{Hilliness::UNDEFINED};
    int32_t feature_id{-1};   // -1 = 不屬於任何 feature
    float water_depth{0.0f};  // 水體深度，僅 OCEAN/COAST/LAKE 有意義
};

// Forward-declare for TileMap.features
struct WorldFeatures;

class TileMap {
public:
    // 預設建構產生 0×0 空地圖（供 WorldGenResult aggregate init 使用）
    TileMap() noexcept = default;
    explicit TileMap(int width, int height, uint16_t default_terrain = TerrainType::PLAINS);

    [[nodiscard]] int width()  const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int size()   const noexcept { return width_ * height_; }

    [[nodiscard]] bool in_bounds(const Hex& h) const noexcept {
        return h.q >= 0 && h.q < width_ && h.r >= 0 && h.r < height_;
    }

    [[nodiscard]] Tile*       get(const Hex& h)       noexcept;
    [[nodiscard]] const Tile* get(const Hex& h) const noexcept;

    void set_terrain(const Hex& h, uint16_t terrain);

    [[nodiscard]] std::vector<Hex> neighbors(const Hex& h)          const;
    [[nodiscard]] std::vector<Hex> passable_neighbors(const Hex& h) const;

    void fill(uint16_t terrain);

    // Iteration helpers
    void for_each(std::function<void(const Hex&, Tile&)> fn);
    void for_each(std::function<void(const Hex&, const Tile&)> fn) const;

    // Direct indexed access (no bounds check)
    Tile&       tile_at(int q, int r)       noexcept { return tiles_[r * width_ + q]; }
    const Tile& tile_at(int q, int r) const noexcept { return tiles_[r * width_ + q]; }

    // features：Phase 6 apply_features() 後填入；nullptr 表示尚未執行
    std::shared_ptr<WorldFeatures> features;

private:
    int width_{0}, height_{0};
    std::vector<Tile> tiles_;
};

// Convenience wrappers using DEFAULT_REGISTRY
[[nodiscard]] float terrain_cost(uint16_t terrain_id);
[[nodiscard]] bool  is_passable(uint16_t terrain_id);

} // namespace mapcore

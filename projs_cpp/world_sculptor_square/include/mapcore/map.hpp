#pragma once
// 地圖容器：2D array 儲存 Tile 的矩形方格地圖。
// 對齊 Python mapcore_py_square 版設計：_rows[y][x] → flat std::vector<Tile> 索引 y*width+x。
// Tile.terrain 儲存 uint16_t id，與 TerrainRegistry 對應。

#include "mapcore/grid.hpp"
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
    // rivers：2 × 8-bit 流量打包進 uint32_t（低 16 bit）
    // bits 0-7  = E (dir 0), bits 8-15 = N (dir 1)
    // W/S 兩條邊由鄰居儲存。詳見 rivers.hpp。
    uint32_t rivers{0};
    Hilliness hilliness{Hilliness::UNDEFINED};
    int32_t feature_id{-1};
    float water_depth{0.0f};
};

// Forward-declare for TileMap.features
struct WorldFeatures;

class TileMap {
public:
    TileMap() noexcept = default;
    explicit TileMap(int width, int height, uint16_t default_terrain = TerrainType::PLAINS);

    [[nodiscard]] int width()  const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int size()   const noexcept { return width_ * height_; }

    [[nodiscard]] bool in_bounds(const Coord& c) const noexcept {
        return c.x >= 0 && c.x < width_ && c.y >= 0 && c.y < height_;
    }

    [[nodiscard]] Tile*       get(const Coord& c)       noexcept;
    [[nodiscard]] const Tile* get(const Coord& c) const noexcept;

    void set_terrain(const Coord& c, uint16_t terrain);

    [[nodiscard]] std::vector<Coord> neighbors(const Coord& c)          const;
    [[nodiscard]] std::vector<Coord> passable_neighbors(const Coord& c) const;

    void fill(uint16_t terrain);

    void for_each(std::function<void(const Coord&, Tile&)> fn);
    void for_each(std::function<void(const Coord&, const Tile&)> fn) const;

    Tile&       tile_at(int x, int y)       noexcept { return tiles_[y * width_ + x]; }
    const Tile& tile_at(int x, int y) const noexcept { return tiles_[y * width_ + x]; }

    std::shared_ptr<WorldFeatures> features;

private:
    int width_{0}, height_{0};
    std::vector<Tile> tiles_;
};

[[nodiscard]] float terrain_cost(uint16_t terrain_id);
[[nodiscard]] bool  is_passable(uint16_t terrain_id);

} // namespace mapcore

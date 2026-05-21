#include "mapcore/map.hpp"
#include <stdexcept>

namespace mapcore {

TileMap::TileMap(int width, int height, uint16_t default_terrain)
    : width_(width), height_(height), tiles_(static_cast<size_t>(width * height))
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("width and height must be > 0");
    for (auto& t : tiles_) t.terrain = default_terrain;
}

Tile* TileMap::get(const Hex& h) noexcept {
    if (!in_bounds(h)) return nullptr;
    return &tiles_[h.r * width_ + h.q];
}

const Tile* TileMap::get(const Hex& h) const noexcept {
    if (!in_bounds(h)) return nullptr;
    return &tiles_[h.r * width_ + h.q];
}

void TileMap::set_terrain(const Hex& h, uint16_t terrain) {
    if (!in_bounds(h))
        throw std::out_of_range("Hex out of bounds");
    tiles_[h.r * width_ + h.q].terrain = terrain;
}

std::vector<Hex> TileMap::neighbors(const Hex& h) const {
    std::vector<Hex> out;
    out.reserve(6);
    for (const auto& d : DIRECTIONS) {
        Hex n = h + d;
        if (in_bounds(n)) out.push_back(n);
    }
    return out;
}

std::vector<Hex> TileMap::passable_neighbors(const Hex& h) const {
    std::vector<Hex> out;
    out.reserve(6);
    for (const auto& d : DIRECTIONS) {
        Hex n = h + d;
        if (in_bounds(n) && is_passable(tiles_[n.r * width_ + n.q].terrain))
            out.push_back(n);
    }
    return out;
}

void TileMap::fill(uint16_t terrain) {
    for (auto& t : tiles_) t.terrain = terrain;
}

void TileMap::for_each(std::function<void(const Hex&, Tile&)> fn) {
    for (int r = 0; r < height_; ++r)
        for (int q = 0; q < width_; ++q)
            fn(Hex{q, r}, tiles_[r * width_ + q]);
}

void TileMap::for_each(std::function<void(const Hex&, const Tile&)> fn) const {
    for (int r = 0; r < height_; ++r)
        for (int q = 0; q < width_; ++q)
            fn(Hex{q, r}, tiles_[r * width_ + q]);
}

float terrain_cost(uint16_t terrain_id) {
    return get_default_registry().move_cost(terrain_id);
}

bool is_passable(uint16_t terrain_id) {
    return get_default_registry().is_passable(terrain_id);
}

} // namespace mapcore

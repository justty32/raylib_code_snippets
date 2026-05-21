// mapcore_cpp_square 簡易測試入口。
// 建置: cmake -B build && cmake --build build
// 執行: ./build/mapcore_square_tests

#include "mapcore/grid.hpp"
#include "mapcore/terrain.hpp"
#include "mapcore/map.hpp"
#include "mapcore/pathfinding.hpp"
#include "mapcore/rivers.hpp"
#include "mapcore/generation/pipeline.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace mapcore;
using namespace mapcore::generation;

static int g_passed = 0, g_failed = 0;
static std::string g_current_suite;

static void check_(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        ++g_failed;
        std::cerr << "  [FAIL] " << expr << " (" << file << ":" << line << ")\n";
    } else {
        ++g_passed;
    }
}

#define CHECK(cond) check_((cond), #cond, __FILE__, __LINE__)

static void suite(const char* name) {
    g_current_suite = name;
    std::cout << "\n[" << name << "]\n";
}

// ── Coord (4-connected grid) ─────────────────────────────────────────────

static void test_coord_basics() {
    Coord a{1, 2}, b{-1, 0};
    CHECK((a + b) == Coord(0, 2));
    CHECK((a - b) == Coord(2, 2));
    CHECK((a * 3)  == Coord(3, 6));
}

static void test_grid_distance() {
    // Manhattan distance
    CHECK(grid_distance({0,0}, {3,0}) == 3);
    CHECK(grid_distance({0,0}, {0,3}) == 3);
    CHECK(grid_distance({0,0}, {3,4}) == 7);  // |3|+|4|
    CHECK(grid_distance({0,0}, {-2,-2}) == 4);
    CHECK(grid_distance({0,0}, {0,0}) == 0);
}

static void test_grid_ring() {
    // 菱形環：radius=r 共 4r 格
    auto r2 = grid_ring({0,0}, 2);
    CHECK(r2.size() == 8u);
    auto r3 = grid_ring({0,0}, 3);
    CHECK(r3.size() == 12u);
    auto r0 = grid_ring({0,0}, 0);
    CHECK(r0.size() == 1u && r0[0] == Coord(0,0));
}

static void test_grid_spiral() {
    // 1 + 2N(N+1)；N=2 → 1+12=13
    auto s = grid_spiral({0,0}, 2);
    CHECK(s.size() == 13u);
    auto s3 = grid_spiral({0,0}, 3);
    CHECK(s3.size() == 25u);  // 1 + 2*3*4
}

static void test_grid_line() {
    // 4-connected supercover line：長度 = Manhattan + 1
    auto l = grid_line({0,0}, {3,0});
    CHECK(l.size() == 4u);
    CHECK(l[0] == Coord(0,0) && l[3] == Coord(3,0));

    auto diag = grid_line({0,0}, {2,2});
    CHECK(diag.size() == 5u);  // 4 + 1
    CHECK(diag.front() == Coord(0,0) && diag.back() == Coord(2,2));
}

static void test_coord_neighbor_wrap() {
    Coord c{0, 0};
    CHECK(c.neighbor(4) == c.neighbor(0));   // wrap 4→0
    CHECK(c.neighbor(-1) == c.neighbor(3));  // wrap -1→3
}

static void test_coord_neighbors_4() {
    Coord c{5, 5};
    auto nb = c.neighbors();
    CHECK(nb.size() == 4u);
    // 0=E, 1=N, 2=W, 3=S
    CHECK(nb[0] == Coord(6, 5));
    CHECK(nb[1] == Coord(5, 4));
    CHECK(nb[2] == Coord(4, 5));
    CHECK(nb[3] == Coord(5, 6));
}

// ── TerrainRegistry ───────────────────────────────────────────────────────

static void test_terrain_registry_defaults() {
    auto& reg = get_default_registry();
    CHECK(reg.size() == 11u);
    CHECK(reg.is_water(TerrainType::OCEAN));
    CHECK(reg.is_water(TerrainType::COAST));
    CHECK(!reg.is_water(TerrainType::PLAINS));
    CHECK(reg.is_passable(TerrainType::PLAINS));
    CHECK(!reg.is_passable(TerrainType::OCEAN));
    CHECK(reg.has_tag(TerrainType::FOREST, "forest"));
    CHECK(!reg.has_tag(TerrainType::OCEAN, "forest"));
    CHECK(reg.move_cost(TerrainType::PLAINS) == 1.0f);
    CHECK(reg.move_cost(TerrainType::FOREST) == 2.0f);
    CHECK(!std::isfinite(reg.move_cost(TerrainType::OCEAN)));
}

// ── TileMap ───────────────────────────────────────────────────────────────

static void test_tilemap_creation() {
    TileMap m(5, 4, TerrainType::PLAINS);
    CHECK(m.width() == 5 && m.height() == 4 && m.size() == 20);
    CHECK(m.in_bounds({4, 3}));
    CHECK(!m.in_bounds({5, 0}) && !m.in_bounds({0, 4}));
}

static void test_tilemap_set_get() {
    TileMap m(8, 8, TerrainType::PLAINS);
    m.set_terrain({3, 3}, TerrainType::OCEAN);
    const Tile* t = m.get({3, 3});
    CHECK(t && t->terrain == TerrainType::OCEAN);
    CHECK(!m.get({100, 100}));
}

static void test_tilemap_neighbors() {
    TileMap m(10, 10, TerrainType::PLAINS);
    // 角落格：2 個鄰居
    CHECK(m.neighbors({0,0}).size() == 2u);
    // 邊格：3 個鄰居
    CHECK(m.neighbors({0,5}).size() == 3u);
    // 內部格：4 個鄰居
    CHECK(m.neighbors({5,5}).size() == 4u);
}

static void test_tilemap_fill() {
    TileMap m(4, 4, TerrainType::PLAINS);
    m.fill(TerrainType::OCEAN);
    m.for_each([](const Coord&, const Tile& t) {
        assert(t.terrain == TerrainType::OCEAN);
    });
    g_passed++;  // explicit pass if no assert triggered
}

// ── Pathfinding ───────────────────────────────────────────────────────────

static void test_astar_basic() {
    TileMap m(10, 10, TerrainType::PLAINS);
    auto path = astar(m, {0,0}, {3,0});
    CHECK(path.has_value());
    if (path) {
        CHECK(path->front() == Coord(0,0));
        CHECK(path->back()  == Coord(3,0));
        CHECK(static_cast<int>(path->size()) == 4);
    }
}

static void test_astar_blocked() {
    TileMap m(5, 5, TerrainType::PLAINS);
    // 整列不可通行，封堵路徑
    for (int y = 0; y < 5; ++y) m.set_terrain({2, y}, TerrainType::MOUNTAIN);
    auto path = astar(m, {0,0}, {4,0});
    CHECK(!path.has_value());
}

static void test_astar_same_point() {
    TileMap m(5, 5, TerrainType::PLAINS);
    auto path = astar(m, {2,2}, {2,2});
    CHECK(path.has_value() && path->size() == 1u);
}

static void test_path_cost_plains() {
    TileMap m(5, 5, TerrainType::PLAINS);
    auto path = astar(m, {0,0}, {2,0});
    CHECK(path.has_value());
    if (path) {
        float cost = path_cost(m, *path);
        CHECK(cost == static_cast<float>(path->size() - 1));  // PLAINS cost=1.0
    }
}

// ── Rivers (2-slot, 4-direction) ─────────────────────────────────────────

static void test_river_edge_set_get() {
    TileMap m(10, 10, TerrainType::PLAINS);
    Coord c{3, 3};
    // dir 0 (E) → 自己 slot 0
    set_river_strength(m, c, 0, 42);
    CHECK(get_river_strength(m, c, 0) == 42);
    // dir 2 (W) → 西邊鄰居 slot 0
    set_river_strength(m, c, 2, 7);
    CHECK(get_river_strength(m, c, 2) == 7);
    // 鏡像驗證：c 的 W 邊 == (c.x-1, c.y) 的 E 邊
    CHECK(get_river_strength(m, {2, 3}, 0) == 7);
}

static void test_river_add_flow() {
    TileMap m(10, 10, TerrainType::PLAINS);
    Coord c{5, 5};
    add_river_flow(m, c, 1, 10);   // N 邊 slot 1
    add_river_flow(m, c, 1, 15);
    CHECK(get_river_strength(m, c, 1) == 25);
}

static void test_river_has_edge() {
    TileMap m(10, 10, TerrainType::PLAINS);
    Coord c{4, 4};
    CHECK(!has_river_edge(m, c, 0));
    set_river_edge(m, c, 0, true);
    CHECK(has_river_edge(m, c, 0));
    set_river_edge(m, c, 0, false);
    CHECK(!has_river_edge(m, c, 0));
}

static void test_river_classify() {
    CHECK(classify_river_strength(1)   == RiverClass::CREEK);
    CHECK(classify_river_strength(79)  == RiverClass::CREEK);
    CHECK(classify_river_strength(80)  == RiverClass::RIVER);
    CHECK(classify_river_strength(159) == RiverClass::RIVER);
    CHECK(classify_river_strength(160) == RiverClass::LARGE_RIVER);
    CHECK(classify_river_strength(255) == RiverClass::LARGE_RIVER);
}

// ── Generation Pipeline ───────────────────────────────────────────────────

static void test_generate_world_smoke() {
    WorldGenResult res = generate_world(20, 15, 42ULL);
    CHECK(res.tile_map.width() == 20 && res.tile_map.height() == 15);
    const int N = 20 * 15;
    CHECK(static_cast<int>(res.heightmap.size()) == N);
    CHECK(static_cast<int>(res.moisture.size())  == N);
    int ocean = 0, land = 0;
    res.tile_map.for_each([&](const Coord&, const Tile& t) {
        if (get_default_registry().is_water(t.terrain)) ++ocean;
        else ++land;
    });
    CHECK(ocean > 0 && land > 0);
}

static void test_generate_world_climate_grids() {
    WorldGenParams params;
    params.climate = true;
    WorldGenResult res = generate_world(15, 12, 123ULL, params);
    int N = 15 * 12;
    CHECK(static_cast<int>(res.temperature_celsius.size()) == N);
    CHECK(static_cast<int>(res.rainfall_mm.size()) == N);
    CHECK(res.has_climate());
}

static void test_generate_world_island_shape() {
    WorldGenParams params;
    params.heightmap_params.shape = "island";
    params.heightmap_params.shape_strength = 0.9f;
    WorldGenResult res = generate_world(30, 20, 999ULL, params);
    int ocean_count = 0;
    res.tile_map.for_each([&](const Coord&, const Tile& t) {
        if (t.terrain == TerrainType::OCEAN || t.terrain == TerrainType::COAST)
            ++ocean_count;
    });
    CHECK(ocean_count > res.tile_map.size() / 5);
}

static void test_generate_world_no_climate() {
    WorldGenParams params;
    params.climate = false;
    params.rivers  = false;
    WorldGenResult res = generate_world(10, 8, 7ULL, params);
    CHECK(res.temperature_celsius.empty());
    CHECK(res.rainfall_mm.empty());
    CHECK(!res.has_climate());
}

// ── main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== mapcore_cpp_square tests ===\n";

    suite("Coord");
    test_coord_basics();
    test_grid_distance();
    test_grid_ring();
    test_grid_spiral();
    test_grid_line();
    test_coord_neighbor_wrap();
    test_coord_neighbors_4();

    suite("Terrain");
    test_terrain_registry_defaults();

    suite("TileMap");
    test_tilemap_creation();
    test_tilemap_set_get();
    test_tilemap_neighbors();
    test_tilemap_fill();

    suite("Pathfinding");
    test_astar_basic();
    test_astar_blocked();
    test_astar_same_point();
    test_path_cost_plains();

    suite("Rivers");
    test_river_edge_set_get();
    test_river_add_flow();
    test_river_has_edge();
    test_river_classify();

    suite("Generation");
    test_generate_world_smoke();
    test_generate_world_climate_grids();
    test_generate_world_island_shape();
    test_generate_world_no_climate();

    std::cout << "\n=== " << g_passed << " passed, " << g_failed << " failed ===\n";
    return g_failed > 0 ? 1 : 0;
}

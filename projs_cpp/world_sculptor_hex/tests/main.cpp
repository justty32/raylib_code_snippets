// mapcore_cpp 簡易測試入口。
// 建置: cmake -B build && cmake --build build
// 執行: ./build/mapcore_tests

#include "mapcore/hex.hpp"
#include "mapcore/terrain.hpp"
#include "mapcore/map.hpp"
#include "mapcore/pathfinding.hpp"
#include "mapcore/rivers.hpp"
#include "mapcore/generation/pipeline.hpp"
#include "mapcore/generation/overlay.hpp"

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

// ── Hex ──────────────────────────────────────────────────────────────────

static void test_hex_basics() {
    Hex a{1, 2}, b{-1, 0};
    CHECK((a + b) == Hex(0, 2));
    CHECK((a - b) == Hex(2, 2));
    CHECK((a * 3)  == Hex(3, 6));
    CHECK(a.s() == -3);
}

static void test_hex_distance() {
    CHECK(hex_distance({0,0}, {3,0}) == 3);
    CHECK(hex_distance({0,0}, {0,3}) == 3);
    CHECK(hex_distance({0,0}, {2,-2}) == 2);
    CHECK(hex_distance({0,0}, {0,0}) == 0);
}

static void test_hex_round() {
    Hex h = hex_round(0.5f, 0.5f);
    CHECK(h.q + h.r + h.s() == 0);
}

static void test_hex_ring() {
    auto r2 = hex_ring({0,0}, 2);
    CHECK(r2.size() == 12u);
    auto r0 = hex_ring({0,0}, 0);
    CHECK(r0.size() == 1u && r0[0] == Hex(0,0));
}

static void test_hex_spiral() {
    auto s = hex_spiral({0,0}, 2);
    CHECK(s.size() == 19u);  // 1 + 6 + 12
}

static void test_hex_line() {
    auto l = hex_line({0,0}, {3,0});
    CHECK(l.size() == 4u);
    CHECK(l[0] == Hex(0,0) && l[3] == Hex(3,0));
}

static void test_hex_neighbor_wrap() {
    Hex h{0, 0};
    CHECK(h.neighbor(6) == h.neighbor(0));   // wrap 6→0
    CHECK(h.neighbor(-1) == h.neighbor(5));  // wrap -1→5
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
    // 角落格
    CHECK(m.neighbors({0,0}).size() <= 3u);
    // 內部格
    CHECK(m.neighbors({5,5}).size() == 6u);
}

static void test_tilemap_fill() {
    TileMap m(4, 4, TerrainType::PLAINS);
    m.fill(TerrainType::OCEAN);
    m.for_each([](const Hex&, const Tile& t) {
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
        CHECK(path->front() == Hex(0,0));
        CHECK(path->back()  == Hex(3,0));
        CHECK(static_cast<int>(path->size()) >= 4);
    }
}

static void test_astar_blocked() {
    TileMap m(5, 5, TerrainType::PLAINS);
    // 整列不可通行，封堵路徑
    for (int r = 0; r < 5; ++r) m.set_terrain({2, r}, TerrainType::MOUNTAIN);
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

// ── Rivers ────────────────────────────────────────────────────────────────

static void test_river_edge_set_get() {
    TileMap m(10, 10, TerrainType::PLAINS);
    Hex h{3, 3};
    set_river_strength(m, h, 0, 42);
    CHECK(get_river_strength(m, h, 0) == 42);
    set_river_strength(m, h, 3, 7);
    CHECK(get_river_strength(m, h, 3) == 7);
}

static void test_river_add_flow() {
    TileMap m(10, 10, TerrainType::PLAINS);
    Hex h{5, 5};
    add_river_flow(m, h, 1, 10);
    add_river_flow(m, h, 1, 15);
    CHECK(get_river_strength(m, h, 1) == 25);
}

static void test_river_has_edge() {
    TileMap m(10, 10, TerrainType::PLAINS);
    Hex h{4, 4};
    CHECK(!has_river_edge(m, h, 0));
    set_river_edge(m, h, 0, true);
    CHECK(has_river_edge(m, h, 0));
    set_river_edge(m, h, 0, false);
    CHECK(!has_river_edge(m, h, 0));
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
    // 至少有陸地和海洋
    int ocean = 0, land = 0;
    res.tile_map.for_each([&](const Hex&, const Tile& t) {
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
    res.tile_map.for_each([&](const Hex&, const Tile& t) {
        if (t.terrain == TerrainType::OCEAN || t.terrain == TerrainType::COAST)
            ++ocean_count;
    });
    // island 形狀邊緣大量是海洋；至少 20% 是水域
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

static void test_overlay_patch_basic() {
    // 衍生地形 id=100：MAGICAL_FOREST
    constexpr uint16_t MAGIC = 100;
    if (!get_default_registry().contains(MAGIC)) {
        get_default_registry().register_def({MAGIC, "MAGICAL_FOREST", 2.0f, false, {"land", "forest", "magic"}});
    }
    WorldGenParams params;
    params.extra_noise_specs.push_back({"magic", 1});
    WorldGenResult res = generate_world(20, 15, 42ULL, params);

    TerrainPatch patch;
    patch.derived_terrain   = MAGIC;
    patch.base_terrain_tags = {"forest"};
    patch.noise_channel     = "magic";
    patch.noise_min         = 0.5f;
    patch.probability       = 1.0f;
    int changed = apply_terrain_patches(res, {patch});
    CHECK(changed >= 0);

    int magic_count = 0;
    res.tile_map.for_each([&](const Hex&, const Tile& t) {
        if (t.terrain == MAGIC) ++magic_count;
    });
    std::cout << "    [INFO] MAGICAL_FOREST tiles: " << magic_count << "\n";
    CHECK(magic_count == changed);
}

// ── main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== mapcore_cpp tests ===\n";

    suite("Hex");
    test_hex_basics();
    test_hex_distance();
    test_hex_round();
    test_hex_ring();
    test_hex_spiral();
    test_hex_line();
    test_hex_neighbor_wrap();

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
    test_overlay_patch_basic();

    std::cout << "\n=== " << g_passed << " passed, " << g_failed << " failed ===\n";
    return g_failed > 0 ? 1 : 0;
}

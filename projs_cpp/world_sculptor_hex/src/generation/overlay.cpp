#include "mapcore/generation/overlay.hpp"
#include "mapcore/generation/pipeline.hpp"  // WorldGenResult 完整定義
#include "mapcore/features.hpp"
#include <algorithm>
#include <random>
#include <unordered_set>

namespace mapcore {
namespace generation {

// ── 條件判斷 ─────────────────────────────────────────────────────────────

static bool base_filter(uint16_t terrain, const TerrainPatch& patch, const TerrainRegistry& reg) {
    if (patch.base_terrain_ids.empty() && patch.base_terrain_tags.empty()) return true;
    if (patch.base_terrain_ids.count(terrain)) return true;
    for (const auto& tag : patch.base_terrain_tags)
        if (reg.has_tag(terrain, tag)) return true;
    return false;
}

static bool noise_pass(const Hex& h, int W, const TerrainPatch& patch, const WorldGenResult& world) {
    if (patch.noise_channel.empty()) return true;
    auto it = world.extra_noise.find(patch.noise_channel);
    if (it == world.extra_noise.end()) return false;
    float val = it->second[h.r * W + h.q];
    return val >= patch.noise_min && val <= patch.noise_max;
}

static bool climate_pass(const Hex& h, int W, const TerrainPatch& patch, const WorldGenResult& world) {
    if (patch.temp_min || patch.temp_max) {
        if (world.temperature_celsius.empty()) return false;
        float t = world.temperature_celsius[h.r * W + h.q];
        if (patch.temp_min && t < *patch.temp_min) return false;
        if (patch.temp_max && t > *patch.temp_max) return false;
    }
    if (patch.rainfall_min || patch.rainfall_max) {
        if (world.rainfall_mm.empty()) return false;
        float r = world.rainfall_mm[h.r * W + h.q];
        if (patch.rainfall_min && r < *patch.rainfall_min) return false;
        if (patch.rainfall_max && r > *patch.rainfall_max) return false;
    }
    return true;
}

static bool adjacency_pass(const Hex& h, const TerrainPatch& patch, const TileMap& map, const TerrainRegistry& reg) {
    if (patch.near_terrain_tags.empty()) return true;
    // BFS within near_radius
    std::unordered_set<int> visited;
    const int W = map.width();
    visited.insert(h.r * W + h.q);
    std::vector<Hex> frontier = {h};
    for (int step = 0; step < patch.near_radius; ++step) {
        std::vector<Hex> next;
        for (const auto& cur : frontier) {
            for (const auto& d : DIRECTIONS) {
                Hex nb = cur + d;
                if (!map.in_bounds(nb)) continue;
                int ni = nb.r * W + nb.q;
                if (visited.count(ni)) continue;
                visited.insert(ni);
                const Tile* nt = map.get(nb);
                if (!nt) continue;
                for (const auto& tag : patch.near_terrain_tags)
                    if (reg.has_tag(nt->terrain, tag)) return true;
                next.push_back(nb);
            }
        }
        frontier = std::move(next);
    }
    return false;
}

static bool hilliness_pass(const Tile& tile, const TerrainPatch& patch) {
    if (patch.hilliness_filter.empty()) return true;
    return patch.hilliness_filter.count(static_cast<int>(tile.hilliness)) > 0;
}

static bool feature_pass(const Tile& tile, const TerrainPatch& patch, const TileMap& map) {
    if (patch.feature_types.empty()) return true;
    if (!map.features || tile.feature_id < 0) return false;
    const WorldFeature* f = map.features->get(tile.feature_id);
    return f && patch.feature_types.count(f->feature_type) > 0;
}

// BFS 過濾連通塊大小 < min_size 的格子
static std::vector<Hex> filter_by_patch_size(
    const std::vector<Hex>& candidates, const TileMap& map, int min_size)
{
    const int W = map.width();
    std::unordered_set<int> cand_set;
    for (const auto& h : candidates) cand_set.insert(h.r * W + h.q);
    std::unordered_set<int> visited;
    std::vector<Hex> result;
    for (const auto& h : candidates) {
        int key = h.r * W + h.q;
        if (visited.count(key)) continue;
        std::vector<Hex> comp;
        std::vector<Hex> stk = {h};
        visited.insert(key);
        while (!stk.empty()) {
            Hex cur = stk.back(); stk.pop_back();
            comp.push_back(cur);
            for (const auto& d : DIRECTIONS) {
                Hex nb = cur + d;
                if (!map.in_bounds(nb)) continue;
                int ni = nb.r * W + nb.q;
                if (visited.count(ni) || !cand_set.count(ni)) continue;
                visited.insert(ni);
                stk.push_back(nb);
            }
        }
        if (static_cast<int>(comp.size()) >= min_size)
            for (const auto& c : comp) result.push_back(c);
    }
    return result;
}

// ── 主入口 ──────────────────────────────────────────────────────────────────

int apply_terrain_patches(
    WorldGenResult& world,
    const std::vector<TerrainPatch>& patches,
    std::optional<uint64_t> seed)
{
    TileMap& tile_map = world.tile_map;
    const TerrainRegistry& reg = world.registry ? *world.registry : get_default_registry();
    uint64_t base_seed = seed.has_value() ? *seed : (world.seed.has_value() ? *world.seed : 0ULL);
    const int W = tile_map.width();
    int total_changed = 0;

    for (const auto& patch : patches) {
        std::mt19937 rng(static_cast<uint32_t>(base_seed + patch.seed_offset));
        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
        std::vector<Hex> candidates;

        tile_map.for_each([&](const Hex& h, const Tile& tile) {
            if (!base_filter(tile.terrain, patch, reg)) return;
            if (!noise_pass(h, W, patch, world)) return;
            if (!climate_pass(h, W, patch, world)) return;
            if (!adjacency_pass(h, patch, tile_map, reg)) return;
            if (!hilliness_pass(tile, patch)) return;
            if (!feature_pass(tile, patch, tile_map)) return;
            if (dist01(rng) > patch.probability) return;
            candidates.push_back(h);
        });

        if (patch.min_patch_size > 1)
            candidates = filter_by_patch_size(candidates, tile_map, patch.min_patch_size);

        for (const auto& h : candidates)
            tile_map.get(h)->terrain = patch.derived_terrain;
        total_changed += static_cast<int>(candidates.size());
    }
    return total_changed;
}

} // namespace generation
} // namespace mapcore

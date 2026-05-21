#pragma once
// 地形 Overlay Phase（Phase 7）。
// 對齊 RimWorld TerrainPatchMaker：根據 TerrainPatch 清單把特定格子升級為衍生地形。

#include "mapcore/map.hpp"
// pipeline.hpp 在 overlay.hpp 之後才能完整 include（否則循環）；
// overlay.cpp 直接 include pipeline.hpp，此處 forward-declare 避免循環。
namespace mapcore { namespace generation { struct WorldGenResult; } }
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace mapcore {
namespace generation {

struct TerrainPatch {
    uint16_t derived_terrain{0};

    // 基底篩選（兩者皆空 = 接受全部）
    std::unordered_set<uint16_t>    base_terrain_ids;
    std::unordered_set<std::string> base_terrain_tags;

    // Noise 條件
    std::string noise_channel;   // "" = 不用 noise
    float noise_min{0.0f};
    float noise_max{1.0f};
    int   min_patch_size{0};     // 0 = 不檢查連通塊

    // 氣候條件（需要 WorldGenResult 有 climate 資料）
    std::optional<float> temp_min, temp_max;
    std::optional<float> rainfall_min, rainfall_max;

    // 鄰接條件
    std::unordered_set<std::string> near_terrain_tags;
    int near_radius{1};

    // Hilliness 條件（-1 = 不限制）
    std::unordered_set<int> hilliness_filter;

    // Feature 條件
    std::unordered_set<std::string> feature_types;

    // 隨機性
    float probability{1.0f};
    int   seed_offset{0};
};

// 依序套用 patches，回傳實際改動的格數。
int apply_terrain_patches(
    WorldGenResult& world,
    const std::vector<TerrainPatch>& patches,
    std::optional<uint64_t> seed = std::nullopt
);

} // namespace generation
} // namespace mapcore

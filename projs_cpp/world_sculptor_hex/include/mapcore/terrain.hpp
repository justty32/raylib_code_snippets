#pragma once
// 地形定義 (TerrainDef) 與登錄系統 (TerrainRegistry)。
// 對齊 RimWorld 的平坦 TerrainDef + DefDatabase 模式。
//
// C++ 移植要點：
//   TerrainDef      → POD struct；tags 用 unordered_set<string>
//   TerrainRegistry → unordered_map<uint16_t, TerrainDef> + unordered_map<string, uint16_t>
//   gen_default()   → 建立含內建地形 (id 0-10) 的 registry
//   DEFAULT_REGISTRY → Meyer's singleton，透過 get_default_registry() 取用

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdexcept>

namespace mapcore {

constexpr float TERRAIN_IMPASSABLE = std::numeric_limits<float>::infinity();

struct TerrainDef {
    uint16_t id{0};
    std::string name;
    float move_cost{1.0f};
    bool is_water{false};
    std::unordered_set<std::string> tags;
};

class TerrainRegistry {
public:
    void register_def(TerrainDef def);

    [[nodiscard]] const TerrainDef& get(uint16_t id) const;
    [[nodiscard]] const TerrainDef* get_by_name(const std::string& name) const noexcept;
    [[nodiscard]] bool contains(uint16_t id) const noexcept;

    [[nodiscard]] float move_cost(uint16_t id) const;
    [[nodiscard]] bool is_passable(uint16_t id) const;
    [[nodiscard]] bool is_water(uint16_t id) const;
    [[nodiscard]] bool has_tag(uint16_t id, const std::string& tag) const noexcept;

    [[nodiscard]] std::vector<const TerrainDef*> all_defs() const;
    [[nodiscard]] size_t size() const noexcept { return by_id_.size(); }

private:
    std::unordered_map<uint16_t, TerrainDef> by_id_;
    std::unordered_map<std::string, uint16_t> name_to_id_;
};

TerrainRegistry gen_default();

// Meyer's singleton：第一次呼叫時由 gen_default() 建立，後續共用同一個實例。
// 對應 Python 的 DEFAULT_REGISTRY 模組全域變數。
inline TerrainRegistry& get_default_registry() {
    static TerrainRegistry inst = gen_default();
    return inst;
}

} // namespace mapcore

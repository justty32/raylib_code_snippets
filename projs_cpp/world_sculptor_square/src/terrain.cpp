// TerrainRegistry 成員實作：地形定義的登錄與查詢。
// 同時維護 id→def 與 name→id 兩張表，讓 id 與名稱都能 O(1) 查找。
#include "mapcore/terrain.hpp"

namespace mapcore {

// 登錄一筆地形定義；id 與 name 都必須唯一，重複則丟例外 (及早發現設定衝突)。
void TerrainRegistry::register_def(TerrainDef def) {
    if (by_id_.count(def.id))
        throw std::invalid_argument("TerrainDef id=" + std::to_string(def.id) + " already registered as " + by_id_.at(def.id).name);
    if (name_to_id_.count(def.name))
        throw std::invalid_argument("TerrainDef name=" + def.name + " already registered with id=" + std::to_string(name_to_id_.at(def.name)));
    name_to_id_[def.name] = def.id;
    by_id_[def.id] = std::move(def);   // def 之後不再使用，move 進表中
}

// 依 id 取定義；找不到丟 out_of_range (id 屬於程式內部約定，缺漏視為錯誤)。
const TerrainDef& TerrainRegistry::get(uint16_t id) const {
    auto it = by_id_.find(id);
    if (it == by_id_.end())
        throw std::out_of_range("No TerrainDef with id=" + std::to_string(id));
    return it->second;
}

const TerrainDef* TerrainRegistry::get_by_name(const std::string& name) const noexcept {
    auto nit = name_to_id_.find(name);
    if (nit == name_to_id_.end()) return nullptr;
    auto it = by_id_.find(nit->second);
    return (it != by_id_.end()) ? &it->second : nullptr;
}

bool TerrainRegistry::contains(uint16_t id) const noexcept {
    return by_id_.count(id) > 0;
}

float TerrainRegistry::move_cost(uint16_t id) const {
    return get(id).move_cost;
}

// 約定：move_cost 為有限值即可通行；無限大 (TERRAIN_IMPASSABLE) 代表不可通行。
// 海洋/山脈等以 INF 表示，省去額外的 passable 旗標。
bool TerrainRegistry::is_passable(uint16_t id) const {
    return std::isfinite(get(id).move_cost);
}

bool TerrainRegistry::is_water(uint16_t id) const {
    return get(id).is_water;
}

bool TerrainRegistry::has_tag(uint16_t id, const std::string& tag) const noexcept {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return false;
    return it->second.tags.count(tag) > 0;
}

std::vector<const TerrainDef*> TerrainRegistry::all_defs() const {
    std::vector<const TerrainDef*> out;
    out.reserve(by_id_.size());
    for (auto& [id, def] : by_id_) out.push_back(&def);
    return out;
}

TerrainRegistry gen_default() {
    TerrainRegistry reg;
    // 內建地形 id 0-10，對應 Python TerrainType IntEnum 數值
    const float INF = TERRAIN_IMPASSABLE;
    reg.register_def({0,  "OCEAN",     INF,  true,  {"water", "ocean"}});
    reg.register_def({1,  "COAST",     INF,  true,  {"water", "coast"}});
    reg.register_def({2,  "PLAINS",    1.0f, false, {"land", "plains"}});
    reg.register_def({3,  "GRASSLAND", 1.0f, false, {"land", "grassland"}});
    reg.register_def({4,  "DESERT",    1.5f, false, {"land", "desert"}});
    reg.register_def({5,  "TUNDRA",    1.0f, false, {"land", "tundra"}});
    reg.register_def({6,  "SNOW",      2.0f, false, {"land", "snow"}});
    reg.register_def({7,  "FOREST",    2.0f, false, {"land", "forest"}});
    reg.register_def({8,  "HILL",      2.0f, false, {"land", "hill"}});
    reg.register_def({9,  "MOUNTAIN",  INF,  false, {"land", "mountain"}});
    reg.register_def({10, "LAKE",      INF,  true,  {"water", "lake"}});
    return reg;
}

} // namespace mapcore

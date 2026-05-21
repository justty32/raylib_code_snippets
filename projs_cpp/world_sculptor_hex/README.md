# mapcore_cpp

`mapcore_py` 的 C++17 移植版，提供完整的六邊形地圖生成系統。
演算法與 Python 版完全對齊；API 命名亦儘量保持一致。

---

## 目錄

- [建置](#建置)
- [架構概覽](#架構概覽)
- [模組參考](#模組參考)
  - [Hex — 軸座標系統](#hex--軸座標系統)
  - [TerrainRegistry — 地形資料庫](#terrainregistry--地形資料庫)
  - [TileMap — 地圖容器](#tilemap--地圖容器)
  - [Pathfinding — 路徑搜尋](#pathfinding--路徑搜尋)
  - [Rivers — 河流系統](#rivers--河流系統)
  - [Features — 命名大區域](#features--命名大區域)
  - [Generation Pipeline — 世界生成管線](#generation-pipeline--世界生成管線)
  - [Overlay — 地形疊加](#overlay--地形疊加)
- [與 Python 版的主要差異](#與-python-版的主要差異)

---

## 建置

**需求**：CMake ≥ 3.17、C++17 編譯器（MSVC 2019+、GCC 9+、Clang 10+）

```bash
cmake -B build
cmake --build build
# 執行測試
./build/Debug/mapcore_tests      # Windows MSVC
./build/mapcore_tests            # Linux / macOS
```

MSVC 使用者注意：CMakeLists.txt 已加入 `/utf-8` 旗標（處理中文註解），
**必須在全新的 `build/` 目錄**下 configure，避免舊快取殘留。

---

## 架構概覽

```
include/mapcore/
├── hex.hpp              # Hex 座標、方向、距離、ring/spiral/line
├── terrain.hpp          # TerrainDef、TerrainRegistry、get_default_registry()
├── map.hpp              # TerrainType、Hilliness、Tile、TileMap
├── pathfinding.hpp      # astar()、path_cost()
├── rivers.hpp           # 河流存儲、generate_rivers()
├── features.hpp         # WorldFeature、FeatureWorker、apply_features()
└── generation/
    ├── heightmap.hpp    # generate_heightmap()   Phase 1
    ├── classify.hpp     # heightmap_to_tilemap() Phase 2
    ├── biome.hpp        # apply_biomes()         Phase 3
    ├── postprocess.hpp  # post_process()         Phase 4
    ├── depressions.hpp  # fill_depressions()     Phase 4.5
    ├── climate.hpp      # apply_climate()        Phase 5
    ├── overlay.hpp      # TerrainPatch、apply_terrain_patches()  Phase 7
    └── pipeline.hpp     # WorldGenResult、WorldGenParams、generate_world()
```

地圖資料以 **flat `std::vector<Tile>`** 儲存，索引公式為 `r * width + q`（row-major）。

---

## 模組參考

### Hex — 軸座標系統

```cpp
#include "mapcore/hex.hpp"
```

```cpp
Hex a{1, 2}, b{3, -1};
Hex sum = a + b;                       // {4, 1}
int dist = hex_distance({0,0}, {3,2}); // 軸距離
Hex rounded = hex_round(1.4f, 2.7f);   // 浮點 → 整數最近 hex

std::vector<Hex> ring   = hex_ring  ({0,0}, 2); // 半徑 2 的環（12 個）
std::vector<Hex> spiral = hex_spiral({0,0}, 3); // 半徑 3 以內所有 hex（37 個）
std::vector<Hex> line   = hex_line  ({0,0}, {4,0}); // 直線（含端點）
```

**方向常數**：`DIRECTIONS[0..5]` = E / NE / NW / W / SW / SE  
河流邊所有權依此順序固定（dir 0/1/2 由本 tile 儲存，dir 3/4/5 由鄰居儲存）。

---

### TerrainRegistry — 地形資料庫

```cpp
#include "mapcore/terrain.hpp"
```

```cpp
// 取得預設 Registry（Meyer's singleton，11 種內建地形）
TerrainRegistry& reg = get_default_registry();

reg.is_water(TerrainType::OCEAN);         // true
reg.is_passable(TerrainType::MOUNTAIN);   // false
reg.move_cost(TerrainType::FOREST);       // 2.0f
reg.has_tag(TerrainType::FOREST, "forest"); // true

// 自訂地形
constexpr uint16_t MY_TERRAIN = 100;
reg.register_def({MY_TERRAIN, "MAGIC_FOREST", 2.0f, false, {"land","forest","magic"}});
```

**內建地形 ID**（`mapcore::TerrainType` namespace）：

| ID | 名稱       | 可通行 | 水體 |
|----|-----------|--------|------|
| 0  | OCEAN     | ✗      | ✓    |
| 1  | COAST     | ✗      | ✓    |
| 2  | PLAINS    | ✓      | ✗    |
| 3  | GRASSLAND | ✓      | ✗    |
| 4  | DESERT    | ✓      | ✗    |
| 5  | TUNDRA    | ✓      | ✗    |
| 6  | SNOW      | ✓      | ✗    |
| 7  | FOREST    | ✓      | ✗    |
| 8  | HILL      | ✓      | ✗    |
| 9  | MOUNTAIN  | ✗      | ✗    |
| 10 | LAKE      | ✗      | ✓    |

---

### TileMap — 地圖容器

```cpp
#include "mapcore/map.hpp"
```

```cpp
TileMap map(40, 30, TerrainType::PLAINS); // width=40, height=30

map.set_terrain({5, 3}, TerrainType::OCEAN);
const Tile* t = map.get({5, 3});          // nullptr if out of bounds
bool ok = map.in_bounds({5, 3});          // true

// 迭代所有 tile
map.for_each([](const Hex& h, Tile& tile) {
    // ...
});

// 直接索引（無邊界檢查，效能最高）
Tile& t = map.tile_at(q, r);
```

`Tile` 欄位：

| 欄位         | 型別       | 說明                       |
|-------------|-----------|---------------------------|
| `terrain`   | `uint16_t` | 地形 ID                   |
| `rivers`    | `uint32_t` | 3×8-bit 河流流量打包        |
| `hilliness` | `Hilliness`| 地勢起伏（5 級）            |
| `feature_id`| `int32_t`  | -1 = 無 feature           |
| `water_depth`| `float`   | 水深（OCEAN/COAST/LAKE 用）|

---

### Pathfinding — 路徑搜尋

```cpp
#include "mapcore/pathfinding.hpp"
```

```cpp
TileMap map = ...;
auto path = astar(map, {0,0}, {10,5});

if (path.has_value()) {
    float cost = path_cost(map, *path);
    for (const Hex& h : *path) { /* 走路徑 */ }
}
```

- 不可通行地形（`move_cost == inf`）自動排除，包含 OCEAN、COAST、MOUNTAIN、LAKE
- 穿越河流邊時加收 `river_crossing_cost`（預設 +3.0）
- 自訂成本：在 `TerrainRegistry` 調整 `move_cost` 即可

---

### Rivers — 河流系統

```cpp
#include "mapcore/rivers.hpp"
```

**存儲層 API**：

```cpp
// 查詢
bool has = has_river_edge(map, {5,5}, 0);      // 是否有東向河流邊
int  str = get_river_strength(map, {5,5}, 1);  // NE 方向流量 (0~255)

// 設定
set_river_strength(map, {5,5}, 0, 42);
set_river_edge    (map, {5,5}, 2, true);       // NW 方向標記為有河流
add_river_flow    (map, {5,5}, 0, 10);         // 累加流量

// 迭代所有河流邊
iter_river_edges(map, [](const Hex& h, int dir, int strength) {
    // dir = 0/1/2 (E/NE/NW)；W/SW/SE 從鄰居取得
});
```

**生成**（RimWorld 風格）：

```cpp
int edge_count = generate_rivers(map, heightmap, rainfall_mm);
// 或帶完整參數
RiverGenParams p;
p.spawn_flow_threshold = 500.0f;
generate_rivers(map, heightmap, rainfall_mm, /*temperature=*/nullptr, /*seed=*/42ULL, p);
```

**流量分級**：

| 分級         | 流量值    |
|-------------|----------|
| CREEK        | 1 ~ 79   |
| RIVER        | 80 ~ 159 |
| LARGE_RIVER  | 160 ~ 255|

---

### Features — 命名大區域

```cpp
#include "mapcore/features.hpp"
```

```cpp
// 使用預設 worker 集合
auto wf = apply_features(map);

// 自訂 worker 列表
std::vector<std::unique_ptr<FeatureWorker>> workers;
workers.push_back(make_ocean_worker());
workers.push_back(make_island_worker(/*min_size=*/5));
workers.push_back(make_continent_worker(/*top_n=*/2));
auto wf = apply_features(map, &workers);

// 查詢
const WorldFeature* f = wf->get(tile.feature_id);
if (f) std::cout << f->feature_type << ": " << f->name << "\n";
```

**內建 Worker**：

| 工廠函式                         | feature_type 範例           | 說明                         |
|---------------------------------|----------------------------|-----------------------------|
| `make_ocean_worker()`           | `"Ocean"`                  | 每個連通海洋塊                |
| `make_lake_worker()`            | `"Lake"`                   | 每個連通湖泊塊                |
| `make_coast_worker()`           | `"Coast"`                  | 每個連通海岸塊                |
| `make_mountain_range_worker(n)` | `"MountainRange"`          | 山脈連通塊，最小格數 n        |
| `make_biome_region_worker(id,n)`| `"BiomeRegion:FOREST"` 等  | 指定地形的連通塊              |
| `make_island_worker(n)`         | `"Island"`                 | 不接觸地圖邊界的陸地連通塊    |
| `make_continent_worker(k,n)`    | `"Continent"`              | 最大 k 個陸地連通塊           |
| `make_icecap_worker(lat)`       | `"Icecap"`                 | 緯度 ≥ lat 且地形為 SNOW      |

---

### Generation Pipeline — 世界生成管線

```cpp
#include "mapcore/generation/pipeline.hpp"
```

```cpp
// 最簡單用法
WorldGenResult res = generate_world(80, 60, /*seed=*/42ULL);

// 完整參數
WorldGenParams p;
p.sea_level                        = 0.45f;
p.heightmap_params.shape           = "island";   // island/archipelago/pangaea/continents/ring_sea/shattered_archipelago
p.heightmap_params.shape_strength  = 0.9f;
p.heightmap_params.ridge_weight    = 0.6f;       // 山脊 noise 強度
p.heightmap_params.ridge_mode      = "plates";   // "plates" | "global"
p.climate                          = true;
p.rivers                           = true;
p.lake_depressions                 = true;        // Priority-Flood 填窪成湖
p.extra_noise_specs.push_back({"magic", 1});      // 供 overlay 使用的額外 noise 通道

WorldGenResult res = generate_world(80, 60, 42ULL, p);
```

**WorldGenResult 欄位**：

| 欄位                  | 說明                                    |
|----------------------|-----------------------------------------|
| `tile_map`           | 完整地圖，含 Tile.feature_id            |
| `heightmap`          | flat float 陣列，值 ∈ [0,1]             |
| `moisture`           | flat float 陣列，值 ∈ [0,1]             |
| `temperature_celsius`| climate 執行後填入（空 = 未執行）        |
| `rainfall_mm`        | climate 執行後填入（空 = 未執行）        |
| `extra_noise`        | `unordered_map<string, vector<float>>`  |
| `seed`               | 使用的 seed                             |

**生成階段**：

| Phase | 函式                   | 說明                              |
|-------|----------------------|-----------------------------------|
| 1     | `generate_heightmap` | fBm + Voronoi 板塊 + 形狀遮罩      |
| 2     | `heightmap_to_tilemap`| 海平線分類、海岸線擴展             |
| 3     | `apply_biomes`       | 依濕度/高度決定草地/沙漠/雪地/森林  |
| 4     | `post_process`       | 移除孤島/小湖、整理海岸線           |
| 4.5   | `fill_depressions`   | Priority-Flood 填窪 → LAKE         |
| 5     | `apply_climate`      | 溫度/降雨（緯度 + 高度 + 雨影）    |
| 6     | `generate_rivers`    | RimWorld 風格河流生成              |
| 6.5   | `apply_features`     | 命名大區域標記                     |

---

### Overlay — 地形疊加

```cpp
#include "mapcore/generation/overlay.hpp"
#include "mapcore/generation/pipeline.hpp"
```

```cpp
// 將森林地形中 magic noise >= 0.5 的格子升級為魔法森林
constexpr uint16_t MAGIC = 100;
get_default_registry().register_def({MAGIC, "MAGIC_FOREST", 2.0f, false, {"land","forest","magic"}});

TerrainPatch patch;
patch.derived_terrain   = MAGIC;
patch.base_terrain_tags = {"forest"};     // 僅作用於有 "forest" tag 的地形
patch.noise_channel     = "magic";        // 使用 extra_noise["magic"]
patch.noise_min         = 0.5f;
patch.noise_max         = 1.0f;
patch.probability       = 0.8f;           // 80% 機率
patch.min_patch_size    = 3;              // 連通塊 < 3 格的排除
// patch.temp_min / temp_max             // 氣候條件（可選）
// patch.near_terrain_tags               // 鄰接條件（可選）

int changed = apply_terrain_patches(res, {patch});
```

**TerrainPatch 條件欄位**：

| 欄位                | 說明                              |
|--------------------|----------------------------------|
| `base_terrain_ids` | 僅作用於這些 terrain ID（空=全部） |
| `base_terrain_tags`| 僅作用於帶此 tag 的地形（空=全部） |
| `noise_channel`    | extra_noise 鍵名（空=不用 noise） |
| `noise_min/max`    | noise 值範圍篩選                  |
| `temp_min/max`     | 氣溫篩選（需 climate 資料）        |
| `rainfall_min/max` | 降雨篩選（需 climate 資料）        |
| `near_terrain_tags`| 周圍 `near_radius` 步內有此 tag   |
| `hilliness_filter` | 地勢篩選（`int` 對應 Hilliness）   |
| `feature_types`    | 限制在特定 feature_type 的 tile   |
| `probability`      | 隨機通過機率 [0, 1]               |
| `min_patch_size`   | 最小連通塊大小（0 = 不限）         |

---

## 與 Python 版的主要差異

| 面向           | Python 版                        | C++ 版                                        |
|---------------|----------------------------------|----------------------------------------------|
| 地形登記        | `TerrainDef` dataclass           | 同名 struct，`tags` 改為 `unordered_set<string>` |
| `None` 值      | Python `None`                    | `std::optional<T>` 或 `-1`（ID）             |
| 地圖儲存        | `list[list[Tile]]`               | flat `std::vector<Tile>`，索引 `r*W+q`        |
| 河流存儲        | Tile dict                        | `uint32_t` bit-pack（3×8-bit，同 Python bit layout） |
| FeatureWorker  | ABC + `@abstractmethod`          | 純虛擬基類 `FeatureWorker`                   |
| Registry 單例  | module-level `DEFAULT_REGISTRY`  | Meyer's singleton `get_default_registry()`   |
| Overlay 循環引用| 無需處理                          | `overlay.hpp` forward-declare `WorldGenResult`；`.cpp` 再 include `pipeline.hpp` |
| 種子型別        | `int`                            | `uint64_t`（`std::optional<uint64_t>`）      |
| 返回值         | 可返回 `None`                     | `[[nodiscard]]` + `std::optional` 明確表達    |

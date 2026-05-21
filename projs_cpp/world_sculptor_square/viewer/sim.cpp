#include "sim.hpp"

#include "mapcore/generation/classify.hpp"
#include "mapcore/generation/biome.hpp"
#include "mapcore/terrain.hpp"
#include "mapcore/map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace viewer {

// ── Flood fill ─────────────────────────────────────────────────────────────
// 對應 mapcore_py_square/editor/sim/hydrology.py: run_flood_fill()

void run_flood_fill(EditorState& s) {
    const int  w = s.width();
    const int  h = s.height();
    const auto n = static_cast<std::size_t>(w * h);

    std::vector<bool> visited(n, false);

    auto idx = [w](int x, int y) -> std::size_t {
        return static_cast<std::size_t>(y * w + x);
    };

    std::deque<std::pair<int, int>> queue;

    // 種子：手動水源優先；否則以邊界上 ≤ sea_level 的格子作種子
    if (!s.water_sources.empty()) {
        for (const GridCoord& g : s.water_sources) {
            if (s.in_bounds(g.x, g.y) && !visited[idx(g.x, g.y)]) {
                visited[idx(g.x, g.y)] = true;
                queue.emplace_back(g.x, g.y);
            }
        }
    } else {
        for (int x = 0; x < w; ++x) {
            for (int yy : {0, h - 1}) {
                if (s.get_h(x, yy) <= s.sea_level && !visited[idx(x, yy)]) {
                    visited[idx(x, yy)] = true;
                    queue.emplace_back(x, yy);
                }
            }
        }
        for (int y = 1; y < h - 1; ++y) {
            for (int xx : {0, w - 1}) {
                if (s.get_h(xx, y) <= s.sea_level && !visited[idx(xx, y)]) {
                    visited[idx(xx, y)] = true;
                    queue.emplace_back(xx, y);
                }
            }
        }
    }

    constexpr std::array<std::pair<int, int>, 4> kDir{{{1,0},{-1,0},{0,1},{0,-1}}};
    while (!queue.empty()) {
        auto [x, y] = queue.front();
        queue.pop_front();
        for (auto [dx, dy] : kDir) {
            const int nx = x + dx, ny = y + dy;
            if (!s.in_bounds(nx, ny) || visited[idx(nx, ny)]) continue;
            if (s.get_h(nx, ny) <= s.sea_level) {
                visited[idx(nx, ny)] = true;
                queue.emplace_back(nx, ny);
            }
        }
    }
    s.ocean_mask = std::move(visited);
}

// ── Climate ────────────────────────────────────────────────────────────────
// 對應 mapcore_py_square/editor/sim/climate.py: run_climate()

void run_climate(EditorState& s) {
    const int  w = s.width();
    const int  h = s.height();
    const auto n = static_cast<std::size_t>(w * h);

    auto idx = [w](int x, int y) -> std::size_t {
        return static_cast<std::size_t>(y * w + x);
    };

    // ── 溫度 ──
    const float lat_scale = s.sun_angle / 90.0f;
    s.temperature.resize(n);
    for (int y = 0; y < h; ++y) {
        const float lat_frac = (h > 1) ? static_cast<float>(y) / static_cast<float>(h - 1) : 0.5f;
        const float lat_heat = 1.0f - std::abs(lat_frac * 2.0f - 1.0f) * lat_scale;
        for (int x = 0; x < w; ++x) {
            const float elev_penalty = s.get_h(x, y) * 0.45f;
            s.temperature[idx(x, y)] = std::clamp(lat_heat - elev_penalty, 0.0f, 1.0f);
        }
    }

    // ── 降雨（水氣沿風向傳遞 + 雨影效應） ──
    constexpr float kPi = 3.14159265358979323846f;
    const float wind_rad = s.wind_dir * kPi / 180.0f;
    const float wdx =  std::sin(wind_rad);
    const float wdy = -std::cos(wind_rad);

    s.rainfall.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        if (s.ocean_mask[i]) s.rainfall[i] = 1.0f;

    std::vector<int> x_order(static_cast<std::size_t>(w));
    std::vector<int> y_order(static_cast<std::size_t>(h));
    std::iota(x_order.begin(), x_order.end(), 0);
    std::iota(y_order.begin(), y_order.end(), 0);
    if (wdx < 0.0f) std::reverse(x_order.begin(), x_order.end());
    if (wdy < 0.0f) std::reverse(y_order.begin(), y_order.end());

    const float evap_per_step = s.evaporation * 0.08f;
    const int src_dx = -static_cast<int>(std::round(wdx));
    const int src_dy = -static_cast<int>(std::round(wdy));

    for (int iter = 0; iter < 4; ++iter) {
        for (int y : y_order) {
            for (int x : x_order) {
                if (s.ocean_mask[idx(x, y)]) {
                    s.rainfall[idx(x, y)] = 1.0f;
                    continue;
                }
                const int sx = x + src_dx, sy = y + src_dy;
                float incoming = 0.0f, rain_shadow = 0.0f;
                if (s.in_bounds(sx, sy)) {
                    incoming    = s.rainfall[idx(sx, sy)];
                    rain_shadow = std::max(0.0f, s.get_h(x, y) - s.get_h(sx, sy)) * 4.0f;
                }
                const float new_m = std::max(0.0f, incoming * (1.0f - evap_per_step) - rain_shadow);
                s.rainfall[idx(x, y)] = std::max(s.rainfall[idx(x, y)], new_m);
            }
        }
    }
}

// ── Export ─────────────────────────────────────────────────────────────────
// 對應 mapcore_py_square/editor/export.py: export_to_worldgen_result()
// 以 JSON 格式儲存（flat 陣列），方便跨語言讀取。

std::string export_world(const EditorState& s) {
    using namespace mapcore;
    using namespace mapcore::generation;

    TileMap tile_map;
    std::vector<float> moisture;
    try {
        tile_map = heightmap_to_tilemap(s.heightmap(), s.width(), s.height(), s.sea_level, 1);
        moisture = (!s.rainfall.empty() && s.rainfall.size() == s.heightmap().size())
                   ? s.rainfall
                   : std::vector<float>(s.heightmap().size(), 0.5f);
        apply_biomes(tile_map, s.heightmap(), moisture, s.sea_level);
    } catch (const std::exception& e) {
        return std::string("export failed (pipeline): ") + e.what();
    }

    std::ofstream f("exported_world_square.json");
    if (!f) return "export failed: cannot open exported_world_square.json";

    auto write_floats = [&](const std::vector<float>& v) {
        f << '[';
        char buf[32];
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) f << ',';
            std::snprintf(buf, sizeof(buf), "%.4f", v[i]);
            f << buf;
        }
        f << ']';
    };

    auto& reg = get_default_registry();
    f << "{\"width\":" << s.width()
      << ",\"height\":" << s.height()
      << ",\"sea_level\":";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", s.sea_level);
    f << buf;
    f << ",\"heightmap\":";  write_floats(s.heightmap());
    f << ",\"moisture\":";   write_floats(moisture);
    if (!s.temperature.empty()) { f << ",\"temperature\":"; write_floats(s.temperature); }
    if (!s.rainfall.empty())    { f << ",\"rainfall\":";    write_floats(s.rainfall);    }
    f << ",\"terrain\":[";
    bool first = true;
    tile_map.for_each([&](const Coord&, const Tile& t) {
        if (!first) f << ',';
        first = false;
        f << '"' << reg.get(t.terrain).name << '"';
    });
    f << "]}\n";

    return "exported to exported_world_square.json";
}

} // namespace viewer

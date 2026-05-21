cmake_minimum_required(VERSION 3.17)
project(mapcore_square CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: 強制以 UTF-8 解析原始檔（必須在 add_library/add_executable 之前）
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(MAPCORE_SQUARE_SOURCES
    src/grid.cpp
    src/terrain.cpp
    src/map.cpp
    src/pathfinding.cpp
    src/rivers.cpp
    src/features.cpp
    src/generation/heightmap.cpp
    src/generation/classify.cpp
    src/generation/biome.cpp
    src/generation/postprocess.cpp
    src/generation/climate.cpp
    src/generation/depressions.cpp
    src/generation/pipeline.cpp
)

add_library(mapcore_square STATIC ${MAPCORE_SQUARE_SOURCES})
target_include_directories(mapcore_square PUBLIC include)

if(MSVC)
    target_compile_options(mapcore_square PRIVATE /W4)
else()
    target_compile_options(mapcore_square PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mapcore_square_tests tests/main.cpp)
target_link_libraries(mapcore_square_tests PRIVATE mapcore_square)

# ── Viewer (raylib + Dear ImGui) ───────────────────────────────────────────
# 預設關閉，避免一般 build 拉 raylib/imgui。開啟方式：
#   cmake -B build -DMAPCORE_SQUARE_BUILD_VIEWER=ON
option(MAPCORE_SQUARE_BUILD_VIEWER "Build the raylib-based square-grid world sculptor viewer" OFF)
if(MAPCORE_SQUARE_BUILD_VIEWER)
    add_subdirectory(viewer)
endif()

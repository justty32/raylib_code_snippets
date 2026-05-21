cmake_minimum_required(VERSION 3.17)
project(mapcore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: 強制以 UTF-8 解析原始檔（必須在 add_library/add_executable 之前）
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(MAPCORE_SOURCES
    src/hex.cpp
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
    src/generation/overlay.cpp
    src/generation/pipeline.cpp
)

add_library(mapcore STATIC ${MAPCORE_SOURCES})
target_include_directories(mapcore PUBLIC include)

if(MSVC)
    target_compile_options(mapcore PRIVATE /W4)
else()
    target_compile_options(mapcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mapcore_tests tests/main.cpp)
target_link_libraries(mapcore_tests PRIVATE mapcore)

# ── Viewer (raylib + Dear ImGui) ───────────────────────────────────────────
# 預設關閉，避免一般 build 拉 raylib/imgui。開啟方式：
#   cmake -B build -DMAPCORE_BUILD_VIEWER=ON
option(MAPCORE_BUILD_VIEWER "Build the raylib-based world sculptor viewer" OFF)
if(MAPCORE_BUILD_VIEWER)
    add_subdirectory(viewer)
endif()

# viewer/CMakeLists.txt — raylib + Dear ImGui + rlImGui
# 由頂層 CMakeLists 在 MAPCORE_BUILD_VIEWER=ON 時 add_subdirectory

include(FetchContent)

# ── raylib ───────────────────────────────────────────────────────────────
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON  CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RMODELS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# ── Dear ImGui (無 CMakeLists；只下載原始碼後自行編譯) ────────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.2b
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# ── rlImGui (raylib ↔ Dear ImGui binding) ────────────────────────────────
FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rlImGui)

add_library(rlImGui STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(rlImGui PUBLIC ${rlimgui_SOURCE_DIR})
target_link_libraries(rlImGui PUBLIC raylib imgui)

# ── Viewer executable ────────────────────────────────────────────────────
add_executable(world_sculptor
    main.cpp
    hex_layout.cpp
    editor_state.cpp
    render.cpp
    tools.cpp
)
target_link_libraries(world_sculptor PRIVATE mapcore rlImGui imgui raylib)
target_compile_features(world_sculptor PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(world_sculptor PRIVATE /W4)
else()
    target_compile_options(world_sculptor PRIVATE -Wall -Wextra)
endif()

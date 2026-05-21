cmake_minimum_required(VERSION 3.17)
project(motion_editor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC：強制以 UTF-8 解析原始檔（注解含中文）
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_executable(motion_editor src/main.cpp)

if(MSVC)
    target_compile_options(motion_editor PRIVATE /W4)
else()
    target_compile_options(motion_editor PRIVATE -Wall -Wextra)
endif()

# raylib：先試 find_package（CMake 套件 / vcpkg），找不到再退回 pkg-config
# （本機開發機 raylib 由系統套件提供，走 pkg-config）。
find_package(raylib QUIET)
if(raylib_FOUND)
    target_link_libraries(motion_editor PRIVATE raylib)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)
    target_link_libraries(motion_editor PRIVATE PkgConfig::RAYLIB)
endif()

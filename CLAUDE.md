# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

這是一個 raylib 遊戲開發程式碼片段集，包含：
- `raylib/` — raylib v6.0.0 C 函式庫原始碼（含 140+ 範例）
- `raylib-cpp/` — raylib 的 C++ OOP 封裝層 v6.0.0（Header-Only）
- `raylib_cheatsheet.md` — raylib C API 完整速查表
- `raymath_cheatsheet.md` — raymath 數學函式庫速查表

## 建置指令

### raylib（C 函式庫）

```powershell
# 建置 raylib
cd raylib
cmake -B build -DBUILD_EXAMPLES=ON
cmake --build build

# 指定平台（Desktop / Web / Android 等）
cmake -B build -DPLATFORM=Desktop -DOPENGL_VERSION=4.3
cmake --build build

# 使用 Makefile（Windows MinGW）
make PLATFORM=PLATFORM_DESKTOP
```

### raylib-cpp（C++ 封裝）

```powershell
# 建置 raylib-cpp 及範例
cd raylib-cpp
cmake -B build -DBUILD_RAYLIB_CPP_EXAMPLES=ON -DBUILD_TESTING=ON
cmake --build build

# 執行測試
cd build && ctest
```

### 編譯單一範例（自訂程式碼）

> **注意（Windows MinGW）**：CMake 預設用 MSVC 建置 `.lib`，MinGW 的 g++ 無法直接使用。
> 請先用 MinGW 建置 raylib（一次性）：
> ```powershell
> cmake -B raylib/build_mingw raylib -G "MinGW Makefiles" `
>   -DCMAKE_MAKE_PROGRAM="C:/dev/mingw64/bin/make.exe" `
>   -DCMAKE_C_COMPILER="C:/dev/mingw64/bin/gcc.exe" `
>   -DPLATFORM=Desktop -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
> cmake --build raylib/build_mingw
> ```
> 產生 `raylib/build_mingw/raylib/libraylib.a`，之後的 `-L` 路徑改用此目錄。

```powershell
# C（使用 MinGW 建置的 raylib）
gcc my_example.c -o my_example -Iraylib/src -Lraylib/build_mingw/raylib -lraylib -lm -lopengl32 -lgdi32 -lwinmm

# C++（header-only，不需額外編譯）
g++ my_example.cpp -o my_example -Iraylib/src -Iraylib-cpp/include -Lraylib/build_mingw/raylib -lraylib -lm -lopengl32 -lgdi32 -lwinmm -std=c++17
```

## 架構

### raylib C 函式庫核心模組

| 檔案 | 職責 |
|------|------|
| `src/raylib.h` | 主 API 標頭（1747 行） |
| `src/rcore.c` | 視窗、事件、計時、輸入、平台抽象 |
| `src/rlgl.h` | OpenGL 1.1～4.3 / ES 2.0/3.0 抽象層 |
| `src/rtextures.c` | 紋理載入與 GPU 管理 |
| `src/rmodels.c` | 3D 模型載入與渲染 |
| `src/raudio.c` | 基於 miniaudio 的音訊系統 |
| `src/rtext.c` | 字型渲染（TTF、OTF、BDF、點陣） |
| `src/rshapes.c` | 2D 圖形繪製 |
| `src/raymath.h` | 向量、矩陣、四元數數學 |
| `src/config.h` | 編譯期功能開關 |

### raylib-cpp 設計模式

所有封裝類別位於 `raylib-cpp/include/`，採用以下設計：

- **RAII 資源管理**：物件建立時自動載入，解構時自動釋放
- **Managed vs Unmanaged**：`Texture` 自動釋放；`TextureUnmanaged` 需手動釋放（用於不持有資源的情況）
- **命名規則**：C 函式移除物件前綴後作為方法名
  ```cpp
  // C：DrawTexture(texture, 50, 50, WHITE);
  // C++：texture.Draw(50, 50, raylib::Color::White());
  ```
- **主命名空間**：所有類別在 `raylib::` 下，`#include "raylib-cpp.hpp"` 引入全部

### 典型遊戲迴圈結構

**C 版本：**
```c
InitWindow(800, 600, "Title");
while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    // 繪製邏輯
    EndDrawing();
}
CloseWindow();
```

**C++ 版本：**
```cpp
raylib::Window window(800, 600, "Title");
while (!window.ShouldClose()) {
    BeginDrawing();
    window.ClearBackground(raylib::Color::RayWhite());
    // 繪製邏輯
    EndDrawing();
}
```

## CMake 重要選項

| 選項 | 說明 |
|------|------|
| `PLATFORM` | 目標平台：Desktop / Web / Android / DRM / SDL |
| `OPENGL_VERSION` | 強制 OpenGL 版本：1.1 / 2.1 / 3.3 / 4.3 / ES 2.0 / ES 3.0 |
| `BUILD_EXAMPLES` | 建置 raylib 範例 |
| `BUILD_SHARED_LIBS` | 建置動態函式庫（預設靜態） |
| `USE_AUDIO` | 啟用音訊支援（預設 ON） |
| `USE_EXTERNAL_GLFW` | 使用系統 GLFW（預設用內建版本） |

## 範例程式碼位置

- `raylib/examples/` — C 語言範例，依功能分類（core / audio / shapes / textures / text / models / shaders）
- `raylib-cpp/examples/` — 對應的 C++ 封裝版本範例

# motion_editor — 2D 動作編輯器

在時間軸上設關鍵幀（拖關節擺姿勢），引擎用每段緩動內插並疊上彈簧（次級動作），
自動補出中間動畫。動作存成純文字 **clip** 檔，可被 AI agent 讀寫；編輯器會**熱載入**
該檔 —— 你在外面跑 agent 改檔，畫面即時更新。

這是 `snippets_cpp/procedural_anim_2d.cpp` 那套程序式動畫引擎的「資料驅動」版本
（手冊裡的「表示法 B」）。骨架規格、座標慣例、clip 檔格式都記在 repo 根目錄的
**`MOTION_AUTHORING.md`**。

## 建置與執行

```bash
# Linux / 系統 raylib（從本資料夾）
g++ src/main.cpp -o motion_editor $(pkg-config --cflags --libs raylib) -std=c++17
./motion_editor clips/sample.clip

# 或用 CMake
cmake -B build && cmake --build build
./build/motion_editor clips/sample.clip
```

> clip 路徑為相對路徑，請在本資料夾執行（預設 `clips/sample.clip`）。

## 操作

| 操作 | 功能 |
|------|------|
| 左鍵拖關節 | 擺姿勢（設定該骨頭角度） |
| 左鍵拖骨盆方塊 | 移動根節點位移（root motion） |
| 時間軸 點 / 拖 | 移動播放頭(scrub)；點關鍵幀標記＝選取該幀 |
| `SPACE` | 播放 / 暫停 |
| `ENTER` | 在播放頭位置 插入 / 覆寫 關鍵幀 |
| `DELETE` | 刪除最接近播放頭的關鍵幀 |
| `,` / `.` | 跳到上一個 / 下一個關鍵幀 |
| `E` | 切換選取幀「到下一幀」的緩動曲線 |
| `-` / `=` | 縮短 / 加長 clip 總長 |
| `L` / `V` / `K` | 切換 loop / 彈簧預覽 / 肌肉顯示 |
| `N` | 新建空白 clip |
| `F5`（或 Ctrl+S） | 存檔 |
| `F9` | 從檔案重新載入 |

編輯時為求精準，顯示的是「原始內插姿勢」（無彈簧）；按 `SPACE` 播放時才疊上彈簧，
可比對加了次級動作後的手感。

## AI agent 工作流程（熱載入）

1. 在編輯器裡擺幾個關鍵幀、存檔（`F5`）。
2. 離開編輯器不必關 —— 在外面對同一個 `*.clip` 檔跑你的 AI agent：把
   `MOTION_AUTHORING.md` 當 context，給它 prompt（例如「在 0.3 到 0.6 之間補一個收手預備、整體更俐落」）。
3. agent 改寫 clip 檔 → 編輯器約 0.4 秒內偵測到並**自動重載**，你立刻看到結果，再微調。

編輯器本身不呼叫 LLM，只負責讀寫/播放 clip 檔；AI 由你在外部驅動，C++ 端零網路相依。

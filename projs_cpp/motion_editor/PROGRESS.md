# 進度與交接（2D 骨架動畫 → 動作編輯器）

> 最近更新：2026-05-21。這份是給人/AI 接手的進度筆記。

## 目標

做動作遊戲，部分動作要用**程序生成**。路線：先有可運作的程序式動畫範例 → 寫手冊讓
AI agent 能生成/調整動作 → 做動作編輯器（時間軸 + 關鍵幀 + AI 補幀）。

## 已完成

### 1. 程序式動畫範例 — `snippets_cpp/procedural_anim_2d.cpp`（已 commit）
側面人形，動作由數學（緩動 + 彈簧）生成：彈簧–阻尼次級動作、緩動驅動、疊加 idle、
根運動位移、X 軸翻面。示範招式：punch / kick / dash / hurt。注解為繁體中文。

### 2. 動作編寫手冊 — `MOTION_AUTHORING.md`（repo 根目錄，已 commit）
可攜式繁中手冊，貼給任何 AI agent 即可生成/調整動作。內容：座標慣例、17 骨完整規格表、
正負號慣例、表示法 A（C++ `author_action`）、表示法 B（關鍵幀 clip 格式 v1）、agent 工作流程。

### 3. 動作編輯器 — `projs_cpp/motion_editor/`（**本次新增，待 commit/push**）
- `src/main.cpp` — 編輯器本體（沿用 procedural_anim_2d 引擎，動作目標改由關鍵幀內插）。
- `clips/sample.clip` — 範例 clip（正面直拳，4 個關鍵幀）。
- `CMakeLists.txt`、`README.md`。
- 功能：拖關節擺姿勢、拖骨盆設根位移、時間軸 scrub/選幀、插入/刪除/覆寫關鍵幀、
  逐幀每段緩動內插、播放時疊彈簧預覽、loop、存讀檔。
- **AI 整合 = clip 檔熱載入**：外部 agent 改 `*.clip`，編輯器約 0.4s 自動重載。編輯器本身不呼叫 LLM。

## 驗證狀態（本機 Manjaro，系統 raylib 6.0.0，pkg-config）

- 三個檔都 `-Wall -Wextra` 0 warnings。
- 編輯器：clip 載入正確（keys=4، duration=0.45）；`sample@0.55 → NUARM=-95, NFARM=8, SPINE=6, root=26`（＝strike 幀，正確）；punch 姿勢、時間軸選幀/scrub 經實機操作正常。

建置：`cd projs_cpp/motion_editor && g++ src/main.cpp -o motion_editor $(pkg-config --cflags --libs raylib) -std=c++17 && ./motion_editor clips/sample.clip`

## 待辦 / 下一步（Phase 3 候選）

- [ ] 編輯器體驗：可拖曳關鍵幀標記改時間；undo/redo；多 clip 切換；onion-skin（前後幀殘影）。
- [ ] clip 格式擴充：每骨各自的緩動、事件標記（命中幀/音效）、根運動「保留 vs 歸位」旗標。
- [ ] 把 `author_action`（表示法 A）也能匯出成 clip（程序動作 → 烤成關鍵幀）。
- [ ] 範例 agent 流程腳本/說明：示範「給 prompt → 改 clip → 熱載入」一輪。
- [ ] 確認 `procedural_anim_2d` 與編輯器共用引擎之後要不要抽成共用 header（目前各自一份，刻意保持 snippet 自包含）。

## 相關檔案速查

```
MOTION_AUTHORING.md                      # 給 AI agent 的手冊（格式/慣例真理來源之一）
snippets_cpp/procedural_anim_2d.cpp      # 程序式動畫引擎範例（繁中注解）
snippets_cpp/skeleton_2d.cpp             # FK/IK/關鍵幀/pose 編輯（同 Bone/FK 核心）
projs_cpp/motion_editor/src/main.cpp     # 動作編輯器
projs_cpp/motion_editor/clips/sample.clip# 範例 clip（格式範本）
```

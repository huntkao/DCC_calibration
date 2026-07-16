# CLAUDE.md — DCC_calibration 專案指引

## 專案是什麼

PDAF DCC(Defocus Conversion Coefficient)校正工具。對單顆相機模組執行:
鏡頭掃描(10 點)→ 逐區相位視差 → 線性回歸出 8×6 DCC map → focus 交叉驗證 → EEPROM 打包。
流程依 Qualcomm 80-NV125-1。目標平台後續會對接 Qualcomm 平台(RB5/QRB5165 經驗背景)。

**前期為離線工具**(2026-07-11 決議):實機擷取、PD 抽取/gain/SAD disparity 計算、
EEPROM 實體燒錄皆由外部模組承擔,本工具消費外部之 disparity/focus 序列
(格式 SPEC-004 §3a;SAD 粒度可 ≠ 8×6,依 config 以 median/加權平均聚合)。
RAW 由外部擷取模組提供,本工具**唯讀**載入作互動式 UI 底圖(不參與計算、不得寫入)。
介面保留待 M2 對接。所有 Phase 參數必須可由 config 設定。

## 目前狀態:M1 完成(2026-07-13),待驗收後進 M2 硬體整合

實作語言 **C++17**;GUI = Dear ImGui v1.91.9b-docking + ImPlot v0.17 + GLFW 3.4
(FetchContent 鎖版;RAW 檢視為 ImPlot 自製,未用 TexInspect——理由見開發紀錄 §2.5)。
測試 = Catch2(80 案例全綠)。CLI(dcc_cal)與 GUI(dcc_gui)能力等價(解耦試金石)。
規格變更仍走 spec 勘誤流程(改檔 + 頂部 revision 一行)。

### 跨機器上手(換手第一件事)
```
cmake -S . -B build -G Ninja && cmake --build build   # 首次需網路拉依賴
ctest --test-dir build                                # 應 80/80 綠
./build/src/dcc_cli/dcc_cal --dry-run                 # CLI 驗證
./build/src/dcc_gui/dcc_gui --smoke                   # GUI 煙霧測試(隱藏視窗)
./build/src/dcc_seqview/dcc_seqview --smoke           # 獨立 disp_seq.json 檢視器煙霧測試
```
每次改動的最低驗證:build 零警告(本專案 targets)→ ctest 全綠 → `--smoke`。

### 環境注意(macOS)
- 新版 macOS 無 PingFang.ttc → 字型退 Hiragino Sans GB + MergeMode 補符號 glyph;
  GUI 首幀有 glyph 自檢(缺字記 log),勿手動猜「?」問題。
- ImPlot heatmap 第一列畫在最上方——餵資料勿翻轉(曾出過鏡射 bug,見開發紀錄 §3.4)。

## 必讀順序(動手前)

1. `docs/開發紀錄_M0-M1.md` — **換手必讀**:重要發現、驗證查核、設計改動與理由、M2 待辦
2. `specs/SPEC-000_專案總覽.md` — 範圍與名詞(SPC/AF 校正是前置輸入,不在 scope)
3. `specs/SPEC-002_校正流程規格.md` — Phase A–G 工序
4. `specs/SPEC-003_架構與模組介面.md` — 分層與介面契約
5. `specs/SPEC-004_資料格式.md` — **單位權威定義,最重要**
6. 領域知識:`docs/DCC校正step-by-step詳解.md`、`docs/PhaseD-1_PD像素解析圖解.html`;
   演算法數學細節(OLS 閉式解、focus 多項式擬合+密集掃描):`docs/演算法數學細節_回歸與focus峰值.html`;
   跨團隊摘要:`docs/M0_階段報告_規格制定與規劃.html`(已發布 GitHub Pages)

## 鐵律(違反 = bug)

1. **單位契約(16 倍陷阱)**:disparity 內部一律 raw_pixel;
   PD 影像格(pd_image_grid)只允許存在於外部序列輸入,
   讀取端(io.disparity_reader)依 `input_disparity_unit` 於入 core 前
   乘 `pitch_x`(範例模組 = 16);打包端依 `output_disparity_unit` 轉出。
   單位轉換全案僅此兩處。DCC 單位固定 DAC/raw_pixel、無號正值。
   任何跨模組函式 docstring 首行必須標注輸入/輸出單位。
2. **分層**:dcc_core/ 純 C++ 演算法(無 I/O、無 UI、無執行緒),不依賴任何其他層;
   依賴方向嚴格單向 gui/cli → app → io/hal → core;
   硬體(馬達/擷取/NVM)一律走 SPEC-003 §4 的介面注入,前期僅 Sim*。
3. **掃描方向**:FAR→NEAR 單向,不可參數反轉(VCM 磁滯)。
4. **失敗處置**:任何中止都要先落盤現場資料(序列快照、DispGrid、參數快照),
   錯誤用 SPEC-001 §3 的具名錯誤碼(滾動式清單)。
   開發初期放寬:允許 Phase 通用碼與簡化落盤,隨里程碑推進逐步嚴格化
   (M1 出口前收斂,見 SPEC-005 §8 #3)。
5. **驗算文化**:尺寸/數量必須閉環驗算(如 288×432 = 15,552×8 = 124,416);
   算不攏就停下來,不要湊數字——本專案曾因此勘誤過一次(見 PhaseD-1 圖解)。

## 假設模組(貫穿所有文件與測試的基準數據)

| 項目 | 值 |
|---|---|
| Sensor | 4608×3456,10-bit,black level 64,metal-shield sparse PDAF |
| Pattern | 32×32,每 block 8 對 L/R(2×4 子格,每格 16×8) |
| pitch | 橫 16 px、縱 8 px → L/R 影像 288×432 |
| VCM | 10-bit DAC;AF_CAL_INF=220、AF_CAL_MACRO=620 |
| Sweep | 由公式推導,禁止寫死:FAR/NEAR = AF ∓/± span×margin(margin 預設 0.1)→ 示例 180→660,10 點,step 53.33(margin=0:220→620,step 44.44) |
| 期望值 | 中央 DCC ≈ 12.46 DAC/px、截距 = 420、focus 峰值 = 420(合成真值,SPEC-005 §2) |
| 容差 | metal-shield 20%(2x1 OCL 15%、dual-PD 10%) |
| Q format | Q6:12.46 → 797 = 0x031D |

## 慣例

- 文件/註解/報告/log 訊息:Traditional Chinese(UTF-8);
  程式識別字與 git commit:English。
- 規格變更:直接改 specs/ 對應檔並在該檔頂部 revision 記錄一行;
  需求增刪要同步 SPEC-005 的測試對映。
- 文件輸出格式偏好:Markdown 為主;HTML 需 light 版面、高對比文字、
  **不得引用外部資源(字型/CDN)**,一律系統字型。
- Commit 風格:`M0:`/`M1:` 前綴標里程碑;規格改動用 `spec:`。

## M1 骨架(C++17,依 SPEC-003 分層)

```
src/dcc_core/  sweep, units, aggregate, regression, focus, validate, eeprom_codec
src/dcc_io/    config, disp_seq_reader, raw_reader, report, eeprom, logging
src/dcc_sim/   synth(合成序列生成;獨立 library,GUI Sim 工作台後端)
src/dcc_hal/   motor, capture, nvm 介面 + Sim* 實作
src/dcc_app/   session_controller(狀態機、失效傳播、背景重算、快照)
src/dcc_cli/   run_calibration(--dry-run 走合成序列)
src/dcc_gui/   ImGui(docking)+ ImPlot + ImGuiTexInspect(vendored)
src/dcc_gui_common/  imgui_bundle 彙整 + 共用主題/字型 header-only(dcc_gui、dcc_seqview 共用)
src/dcc_seqview/     獨立 disp_seq.json 檢視器(外部團隊自檢工具;零依賴 core/io/app/sim,
                     見 seq_loader.hpp 註解;DCC_BUILD_SEQVIEW_GUI 控制是否建置 GUI 執行檔)
tests/         Catch2,對映 SPEC-005 UT-01..10
```
先寫測試(UT 表格是現成的驗收準則),再實作;dry-run(IT-01)綠了才碰硬體層。
(pd_extract/gain/disparity 演算法已移轉外部模組,不在骨架內。)

## 開放問題(已全數關閉或明確延期,不卡 M1;詳見 SPEC-005 §7)

1. Vendor PD offsets vs. 均勻子格假設 —— 延期至 M2 前(主要影響外部 SAD 模組)
2. Runtime PDLIB 的 disparity 單位 —— 延期至 M2 燒錄前(`output_disparity_unit`
   雙路徑已對沖,定案時翻 config 即可)
3. Chart 距離公差對 DCC 的靈敏度 —— 轉為 M1 後首批分析任務(合成序列掃描)
4. 實際 EEPROM layout(`PDAFCalibrationTools_EEPROM.h`)—— **已改性質 2026-07-16**:
   假設格式轉正(非 Qualcomm 專用),`PDAFCalibrationTools_EEPROM.h` 對齊改為條件式(僅對接 Qualcomm 平台時)
5. ~~外部 SAD 模組輸出粒度與聚合規則~~(已關閉 2026-07-11:粒度可設定,
   median/加權平均聚合,見 SPEC-004 §3a、SPEC-002 D-5)

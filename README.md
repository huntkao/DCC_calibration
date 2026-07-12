# DCC_calibration

PDAF DCC(Defocus Conversion Coefficient)校正工具 — **M0 規格已凍結(2026-07-12),M1 開發中(C++17)**。

前期為**離線工具**(2026-07-11 決議):實機擷取、SAD disparity 計算、EEPROM
實體燒錄由外部模組承擔(介面保留待 M2),本工具消費外部之 disparity/focus
序列(SPEC-004 §3a),RAW 僅唯讀載入作互動式 UI 底圖。

> **M0 階段報告(跨團隊閱讀版)**:https://huntkao.github.io/DCC_calibration-report/
> (源檔 `docs/M0_階段報告_規格制定與規劃.html`;規範性內容以 specs/ 為權威)

## 目前狀態:M1 開發階段

```
docs/     原理與教學文件(step-by-step、Phase D-1 圖解勘誤、總覽指南)
specs/    開發規格(本階段的主要產出)
  SPEC-000 專案總覽與範圍
  SPEC-001 系統需求(FR/NFR + 錯誤碼)
  SPEC-002 校正流程規格(Phase A–G 工序化)
  SPEC-003 架構與模組介面(分層、單位契約、HAL)
  SPEC-004 資料格式(config schema、單位權威定義、EEPROM、report)
  SPEC-005 驗證與測試計畫(UT/IT/HT/AT、出場準則)
config/   sensor_config_example.json(對映 SPEC-004 §2)
data/     raw/(擷取暫存)、output/(報告)——實作後啟用
```

## 閱讀順序

1. `docs/DCC校正step-by-step詳解.md` — 領域知識
2. `SPEC-000` → `SPEC-002` — 要做什麼、怎麼流動
3. `SPEC-003`/`SPEC-004` — 介面與單位(**16 倍陷阱在此釘死**)
4. `SPEC-005` — 怎麼證明做對了

## M1 計畫(C++17;GUI = ImGui docking + ImPlot + ImGuiTexInspect;測試 = Catch2)

- [x] M0 規格凍結(2026-07-12;開放問題 #5 關閉、#1/#2/#4 延期 M2、#3 轉分析任務)
- [ ] 與外部 SAD 模組負責方對齊 `disp_seq.json` 格式(SPEC-004 §3a)
- [x] M1a 核心 + CLI:dcc_core/io/app + Sim 生成器;UT-01..10 + IT-01/02/03/04/06/07
      全綠(46 案例);`dcc_cal --dry-run` 端到端可跑(build:`cmake -S . -B build -G Ninja && cmake --build build`)
- [ ] M1b GUI 骨架:docking 佈局、config 面板、pipeline 控制、log 主控台
- [ ] M1c 視覺化:heatmap / 逐區回歸圖 / focus 曲線 / RAW 檢視(TexInspect)
- [ ] M1d 強固化:全 E-code 演練、session 存讀、report 檢視、離線重算

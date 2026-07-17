# SPEC-000 · 專案總覽與範圍

| 項目 | 內容 |
|---|---|
| 專案名稱 | DCC_calibration — PDAF DCC 校正工具 |
| 版本 | Draft 0.2(規格階段;rev 2026-07-11:前期改為離線工具,disparity/focus 由外部模組提供,實機擷取與燒錄僅留介面;rev 2026-07-17:專案**永久**定位離線校正+實驗工具,不做硬體對接——原 M2 硬體整合/M3 產線化移出 scope,見 §6) |
| 依據 | Qualcomm《PDAF Module Calibration Guide》80-NV125-1 Rev. L5 |
| 參考 | `docs/DCC校正step-by-step詳解.md`、`docs/PhaseD-1_PD像素解析圖解.html` |

## 1. 目標

開發一套可在產線/實驗室執行的 DCC(Defocus Conversion Coefficient)校正工具:
對單顆相機模組完成鏡頭掃描、相位視差量測、逐區線性回歸、
交叉驗證與校正資料打包,輸出可燒錄之 DCC map。

## 2. 範圍(Scope)

### In scope
- S1. Metal-shield sparse PDAF 模組之 DCC 校正(首要目標)
- S2. 離線管線:掃描規劃、disparity 序列讀取與驗證(SPEC-004 §3a)、
  逐區回歸、focus 交叉驗證、校正 block 打包(檔案落盤)、報告輸出
- S3. 合成資料模式(dry-run):合成 disparity/focus 序列驗證整條管線
- S4. 校正報告輸出(每模組:DCC map、誤差 map、PASS/FAIL、log)
- S5. RAW 唯讀載入(外部擷取模組提供;供互動式 UI 作影像底圖,選配輸入,
  不參與量測計算,本工具不得寫入)

### Out of scope(本期不做)
- O1. Gain Map(SPC/LRC)校正本身——視為已完成之前置輸入
- O2. AF / OIS 校正——僅消費其結果(`AF_CAL_INF/MACRO`)
- O3. Dual-PD、2x1 OCL 型(介面預留,實作延後)
- O4. Runtime PDLIB 對焦演算法(僅需保證單位/格式相容)
- O5. 實機 RAW 擷取與馬達控制(前期;HAL 介面保留,以 SimHAL 代;
  RAW 檔案由外部擷取模組提供,本工具僅唯讀消費,見 S5)
- O6. PD 抽取、gain 套用、SAD disparity 計算——由外部模組完成,
  本工具消費其逐區 disparity/focus 序列(精度要求見 SPEC-005 §3a)
- O7. EEPROM 實體燒錄(前期;pack/verify 產出 `block.bin`,NvmIF 介面保留)

## 3. 角色與使用情境

| 角色 | 情境 |
|---|---|
| 產線操作員 | 一鍵執行單模組校正,依 PASS/FAIL 分流 |
| 校正工程師 | 調整參數(margin、容差、搜尋窗),分析 FAIL 根因 |
| 演算法工程師 | 以合成模式開發/回歸測試,對接 runtime 單位定義 |

## 4. 名詞定義

| 術語 | 定義 |
|---|---|
| DCC | Δ鏡頭位置[DAC] / Δphase disparity[pixel],無號正值 |
| disparity | R 影像相對 L 影像之水平位移;**單位見 SPEC-004 §3(全案唯一權威定義)** |
| pattern block | PD 配置之最小重複單元(範例:32×32 px) |
| pitch_x / pitch_y | L(或 R)樣本之取樣間距(範例:16 / 8 px) |
| sweep | FAR→NEAR 之 10 點鏡頭掃描 |
| region | 8×6 之回歸分區,各自產出一個 DCC 值 |

## 5. 文件地圖

| 編號 | 內容 |
|---|---|
| SPEC-001 | 系統需求(功能/非功能) |
| SPEC-002 | 校正流程規格(Phase A–G 之工序化定義) |
| SPEC-003 | 軟體架構與模組介面規格 |
| SPEC-004 | 資料格式規格(config schema / 單位 / EEPROM / 報告) |
| SPEC-005 | 驗證與測試計畫(合成資料、驗收準則) |

## 6. 里程碑(建議)

> rev 2026-07-17:專案定位收斂為**離線校正工具 + 實驗用途,不做硬體對接**。
> 原 M2「硬體整合」(實機擷取、外部 SAD 對接、EEPROM 實體燒錄)與 M3「產線化」
> (節拍、HT/AT、CDAF 金標)**移出 scope**;實機與燒錄由外部模組承擔,本工具僅
> 消費其序列並產出離線校正結果與可讀等價檔。M2 改為離線分析/實驗能力擴充。

| M | 內容 | 完成定義 |
|---|---|---|
| M0 | 規格凍結 | 本 SPEC 系列 review 通過 |
| M1 | 離線管線 | 合成/外部 disparity 序列 dry-run 全流程 PASS,單元測試綠 |
| M2 | 分析/實驗擴充 | 離線分析與實驗能力(chart 距離公差靈敏度掃描、Sim 雜訊注入、fitter 精度演練、q→σ 標定等),**不含任何硬體對接** |
| ~~M2 硬體整合~~ | ~~實機擷取/外部 SAD/EEPROM 燒錄~~ | 移出 scope(2026-07-17) |
| ~~M3 產線化~~ | ~~節拍 ≤ 4 s、HT/AT~~ | 移出 scope(2026-07-17) |

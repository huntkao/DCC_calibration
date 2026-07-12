# SPEC-002 · 校正流程規格(工序化)

> rev 2026-07-11:離線前期範圍——Phase B、C-2 之實機動作標 [M2/外部];Phase D 重定義為 disparity 序列載入與驗證(原 pixel 級工序移轉外部模組);Phase F 誤差分母改符號式。
> rev 2026-07-11(2):Phase D 新增 D-5 粒度聚合(median/加權平均,關閉開放問題 #5);Phase B 傾斜檢查之量測條件明定由外部模組規格承接。
> rev 2026-07-11(3):margin 預設定案 0.1;C-1/Phase F 改公式參數化表述,數列僅為預設 config 推導示例。

每一 Phase 定義:進入條件 → 輸入 → 處理 → 輸出 → 失敗處置。
數值範例沿用假設模組(4608×3456 / 32×32 pattern / 8 對 / INF 220 / MACRO 620)。
標 **[M2/外部]** 之工序:前期由外部設備/演算法模組執行,本工具僅定義介面與入料驗證。

---

## Phase A · 前置驗證
- **進入條件**:操作員載入 config 並按「開始」。
- **輸入**:config JSON;NVM 之 gain map;AF 校正結果。
- **處理**:FR-02 之四項檢查;將 config 快照寫入 log。
- **輸出**:`PreflightReport{pass, items[]}`。
- **失敗**:E-A0x;不得進入 Phase B。

## Phase B · 場景確認(半自動)**[M2/外部]**
- **進入條件**:Phase A PASS。
- **輸入**:一張預覽 RAW。
- **處理**:曝光窗檢查(FR-06);chart 覆蓋率估測(有效紋理區佔 FOV 85–95%,
  以區塊紋理能量統計);傾斜/旋轉粗檢(左右半邊 focus value 差 < 15%)。
- **輸出**:`SceneReport`;不合格項目以人可讀訊息提示調整。
- **失敗**:操作員調整後重試;連續 3 次不合格 → 記錄並中止。
- 傾斜/旋轉粗檢之量測條件(如檢查所在之 DAC 位置)由外部設備模組規格
  定義,本工具不約束(2026-07-11 決議)。

## Phase C · 掃描規劃(與擷取介面)
- **C-1 規劃**(本工具,全部由 config 推導,禁止寫死數值):
  - `span_af = MACRO − INF`;`FAR = INF − span_af × far_margin`;
    `NEAR = MACRO + span_af × near_margin`;
    `step = (NEAR − FAR) / (num_positions − 1)`,各點四捨五入、端點精確(FR-03)。
  - 推導示例(INF 220 / MACRO 620 / margin 預設 0.1 / 10 點):
    FAR=180、NEAR=660、step≈53.33,DAC 序列
    `180,233,287,340,393,447,500,553,607,660`;
    (margin=0 時:`220,264,309,353,398,442,487,531,576,620`。)
  規劃結果供外部擷取模組使用,並作為 Phase D 入料驗證基準。
- **C-2 擷取迴圈 [M2/外部]**(i = 1..10):
  1. `motor.move(dac_i)`;等待 settle(預設 30 ms,可 config)。
  2. `capture.grab()` → RAW + metadata。
  3. 入料檢查(FR-06);PASS → 入堆疊,FAIL → E-C0x 中止。
- **時序預算 [M2]**(NFR-01):move+settle 40 ms、grab 60 ms、檢查 10 ms
  → 每點 ≤ 110 ms,10 點 ≤ 1.1 s,餘量留給 Phase D/E。

## Phase D · Disparity 序列載入與驗證(離線)
- **進入條件**:Phase A PASS;`disp_seq.json`(SPEC-004 §3a)存在。
- **輸入**:disparity/focus 序列檔;config。
- **處理**:
  - **D-1 形狀驗證**:N == `num_positions`;data/focus/quality 形狀
    == [N][grid_h][grid_w] 一致(grid 為外部 SAD 粒度,可 ≠ dcc.grid);失敗 → E-D01。
  - **D-2 DAC 對應**:dacs 嚴格遞增(FAR→NEAR,鐵律 3)且與 C-1 規劃
    逐點一致(容差 ±1 DAC);失敗 → E-D02。
  - **D-3 單位**:unit == config `input_disparity_unit`、pitch_x 一致;
    pd_image_grid → ×pitch_x 轉 raw_pixel(**僅此一處轉換**);不符 → E-D02。
  - **D-4 無效樣本標記**:null → NaN(cell 級)。
  - **D-5 粒度聚合**(序列 grid == dcc.grid 時跳過):以 cell 中心座標歸區
    (8×6 等分切割,邊界歸左/上區),依 config `aggregation.method` 聚合
    disparity 與 focus——`median`(預設)或 `weighted_mean`(以 quality 為
    權重,缺則等權;NaN cell 不計);區內有效 cell 比例 <
    `aggregation.min_valid_ratio`(預設 0.5)→ 該區該幀記 NaN。
  - **D-6 有效樣本統計**:逐區(8×6)跨幀有效樣本數
    < `min_valid_samples`(預設 8)→ E-D03。
- **輸出**:`DisparityTensor[10][6][8]`(raw_pixel)+ `QualityTensor[10][6][8]`
  + `FocusValue[10][6][8]`(供 Phase F)。
- **失敗**:E-D0x;落盤已讀入之序列快照與 config。

> 原 pixel 級工序(PD 抽取、增益、垂直平均、SAD、次像素、品質門檻)移轉
> 外部 SAD 模組;其精度驗收要求見 SPEC-005 §3a,原工序規格保留於 docs/
> 供 M2 後對接參考(SPEC-005 §8 備忘 #4)。

## Phase E · 回歸
- 逐區:有效樣本 ≥ 8 → 最小平方擬合;輸出 `dcc[6][8]`、`intercept[6][8]`、
  `r2[6][8]`(擬合品質,< 0.98 警告)。
- 斜率檢查(FR-12)、平滑性檢查(FR-15)。

## Phase F · 驗證
- 逐區 focus 曲線 4 階多項式擬合 → `focus_peak[6][8]`;出界檢查(FR-13)。
- `err[6][8] = |intercept − focus_peak| / (NEAR − FAR)`(分母以 C-1 實際推導之
  sweep span 計,禁止寫死;預設 config 推導示例 = 480);與 tolerance 逐區比較(FR-14)。
- 模組判定:全區 PASS → PASS;否則 FAIL(附最差三區座標與數值)。

## Phase G · 打包與報告
- Q6 編碼 DCC(12.46 → 797 = 0x031D);組 block、checksum;回讀驗證。
- 前期:`block.bin` 落盤 + SimNvm 回讀驗證;實體燒錄屬 M2。
- 報告落盤 `data/output/<module_id>/report.json` + `report.md`。

---

## 流程圖(文字版)

```
離線前期:
A 前置 ──► C-1 規劃 ──► D 序列載入/驗證(10×48)──► E 回歸(48)──► F 驗證 ──► G 打包(檔案)
  │E-A0x                  │E-D0x                      │E-E01         │E-F0x      │E-G01
  └────── 中止 + 錯誤碼 + 現場資料保留(可離線重算)◄──────────────────────────┘

M2 後插入:B 場景確認、C-2 實機擷取 + 外部 SAD → 產出 disp_seq;G 加實體燒錄
```

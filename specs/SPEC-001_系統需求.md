# SPEC-001 · 系統需求規格

> rev 2026-07-11:前期範圍改為離線工具——擷取(FR-05/06)標 [M2/外部],Phase D 需求(FR-08〜11)改為 disparity 序列讀取與驗證;錯誤碼表改滾動制。
> rev 2026-07-11(2):FR-08 支援 SAD 細粒度輸出與聚合(關閉開放問題 #5);新增 FR-18 RAW 唯讀載入(UI 底圖)。
> rev 2026-07-12:M0 凍結後之實作決議勘誤——NFR-04 實作語言改 C++17(原 Python+NumPy)。

需求編號規則:FR-xx 功能需求、NFR-xx 非功能需求。
每條需附驗收方式(SPEC-005 對應測試)。
標 **[M2/外部]** 之需求:前期不實作,僅保留介面與規格,由外部設備/演算法模組承擔。

## 1. 功能需求

### 1.1 設定與前置(Phase A)
- **FR-01** 工具須自設定檔載入 sensor / VCM / DCC 參數(schema 見 SPEC-004 §2;主格式 JSON,ini 為選配對映),缺欄位或非法值須報錯並拒絕啟動。**所有 Phase 之可調參數皆須可由 config 設定**,不得散落於程式常數。
- **FR-02** 工具須驗證前置條件並記錄:AF 校正值存在且 `INF < MACRO`;gain map 可載入;PD offsets 依 x 遞增;orientation 為 canonical。任一不符 → 中止,錯誤碼 E-A0x。

### 1.2 掃描規劃(Phase C-1)
- **FR-03** 依公式計算 sweep:`FAR = INF − span×far_margin`、`NEAR = MACRO + span×near_margin`、10 點等距取整。結果超出 DAC 實體範圍 → E-C01。
- **FR-04** sweep 方向固定 FAR→NEAR 單向(磁滯對策),不可由外部參數反轉。

### 1.3 擷取協調(Phase C-2)**[M2/外部]**
- **FR-05 [M2/外部]** 對每一 sweep 點:下 DAC → 等待 `settle_time_ms` → 觸發全幅 RAW 擷取 → 附掛 metadata(DAC、曝光、gain、時間戳)。
- **FR-06 [M2/外部]** 每張 RAW 須做入料檢查:尺寸、bit depth、飽和比例(一般像素 1023 佔比 < 0.1%)、中央 ROI 之 L/R 平均在 300–950 LSB。不符 → E-C0x,中止並保留現場資料。(black level/曝光相關處理皆屬像素域,由外部模組承擔。)
- **FR-07** 硬體抽象:馬達與擷取以介面注入(SPEC-003 §4);前期一律使用模擬器(SimHAL)。

### 1.4 Disparity 序列讀取與驗證(Phase D)
- **FR-08** 工具須依 SPEC-004 §3a 讀取外部模組之 disparity/focus 序列並驗證:
  維度閉環(N×grid_h×grid_w,data/focus/quality 形狀一致,E-D01)、
  DAC 清單嚴格遞增(FAR→NEAR)且與 Phase C-1 規劃一致(E-D02)、
  unit 與 pitch_x 與 config 一致(E-D02)。
  序列 grid 為外部 SAD 粒度,可 ≠ `dcc.grid`;不一致時依 SPEC-002 D-5 聚合至
  8×6,聚合方法可設定(`median` 預設 / `weighted_mean` 以 quality 加權)。
- **FR-09** 讀取端即依 `input_disparity_unit` 轉為 raw_pixel(pd_image_grid × pitch_x);此後 core 內不得出現第二種單位。
  (gain map 套用已於外部完成;本工具僅將 NVM gain map 透傳至打包,FR-16。)
- **FR-10 [M2/外部]** SAD disparity 計算(抽取、增益、垂直平均、SAD、次像素)由外部模組承擔;其精度驗收要求見 SPEC-005 §3a 誤差預算。
- **FR-11** 無效樣本以 null(或 quality 標記)入料,讀取後以 NaN 表示;單區有效樣本 < `min_valid_samples`(預設 8)→ 該區 FAIL(E-D03)。

### 1.5 回歸與驗證(Phase E/F)
- **FR-12** 逐區最小平方擬合 `DAC = k·disparity + b`;任何區 `k ≤ 0` → E-E01(提示 LEFT/RIGHT 檢查)。
- **FR-13** 逐區以 focus 曲線(多項式擬合)取峰值;峰值落在 sweep 端點 ±1 步內 → E-F01(0x2000 對應)。
- **FR-14** 誤差 = `|b − focus_peak| / (NEAR−FAR)`;任一區 ≥ tolerance(metal-shield 預設 0.20)→ 模組 FAIL。
- **FR-15** DCC map 平滑性檢查:相鄰區差異 > `smooth_limit`(預設 25%)→ 警告並列入報告。

### 1.6 輸出(Phase G)
- **FR-16** 依 SPEC-004 §4 打包校正 block(Q-format、checksum),提供燒錄 buffer 與回讀驗證函式。前期僅落盤 `block.bin` 並經 SimNvm 回讀驗證;實體燒錄屬 M2。
- **FR-17** 產出單模組報告(JSON + 人閱讀版):config 快照、sweep 資料、48 區 DCC/截距/誤差、PASS/FAIL、錯誤碼、耗時。

### 1.7 UI 呈現(前期,互動式介面)
- **FR-18** 工具須能**唯讀**載入外部擷取模組提供之 RAW(路徑/格式見 SPEC-004 §1)
  作為 UI 影像元件底圖。RAW 為選配輸入:非校正計算之依據,缺檔僅降級顯示
  (無底圖),不影響管線執行與 PASS/FAIL;工具在任何情況下不得寫入或修改 RAW。

## 2. 非功能需求
- **NFR-01 節拍**:單模組(10 點,含移動/擷取/運算)≤ 4 s(目標 2 s)。[M2 起適用;離線模式僅計 Phase D–G 運算,目標 ≤ 1 s]
- **NFR-02 重複性**:同模組連續 10 次校正,中央區 DCC 標準差 / 平均 < 2%。
- **NFR-03 可追溯**:每次執行完整 log(參數、原始量測、中間量),可離線重算。
- **NFR-04 可移植**:核心演算法純 C++17(dcc_core:無 I/O、無 UI、無執行緒、標準庫為主);
  硬體層與前端(GUI/CLI)抽換不影響核心;可攜至 Qualcomm 平台(RB5/QRB5165)。
- **NFR-05 語言/編碼**:報告與 log 訊息 Traditional Chinese,UTF-8。

## 3. 錯誤碼總表(滾動式清單)

> 本表為**滾動式**清單:隨開發演進於此增補/細化,SPEC-002 各 Phase 引用之。
> 開發初期允許以 Phase 通用碼(如 E-D00)拋出、待情境穩定後細化為具名碼;
> 鐵律 4(具名錯誤碼 + 現場落盤)於初期放寬,隨里程碑推進逐步嚴格化(SPEC-005 §8 備忘 #3)。

| 碼 | 意義 | 對策提示 |
|---|---|---|
| E-A01 | AF 校正值缺失/非法 | 先完成 AF 校正 |
| E-C01 | sweep 超出 DAC 範圍 | 檢查 margin 或 AF 值 |
| E-C02 | [M2/外部] 曝光超窗(<300 或 >950) | 調光源/曝光 |
| E-C03 | [M2/外部] 一般像素飽和 | 降曝光(blooming 風險) |
| E-D01 | 序列維度/形狀閉環驗算失敗 | 核對 disp_seq 與 config grid |
| E-D02 | 序列 DAC/unit/pitch 與規劃或 config 不一致 | 核對外部模組輸出與 C-1 規劃 |
| E-D03 | 區域有效樣本不足 | chart 覆蓋/髒污/局部過曝 |
| E-E01 | 斜率非正 | LEFT/RIGHT 顛倒 |
| E-F01 | focus 峰值出界(0x2000) | chart 距離/場曲篩選 |
| E-F02 | 誤差超容差 | 依誤差 map 查根因 |
| E-G01 | Q-format 溢位 | 檢查 q 值與 DCC 量級 |

# SPEC-005 · 驗證與測試計畫

> rev 2026-07-11:合成資料改為 disparity/focus 序列生成器;UT-02〜06 依離線範圍重寫並修正 UT-05/06 期望值(對齊合成真值 420);新增 §3a 誤差預算(取代原 UT-03/04 過鬆準則);新增 §8 開發備忘。
> rev 2026-07-11(2):關閉開放問題 #5(SAD 粒度可設定 + median/加權聚合),新增 UT-10。
> rev 2026-07-11(3):margin 預設定案 0.1;UT-01/06、IT-04、§3a 誤差預算改公式參數化並依預設 config 重推示例數字。
> rev 2026-07-11(4):§7 開放問題 #1/#2/#4 標註延期至 M2 前、#3 轉為 M1 後分析任務——M0 出場準則(§6)之「明確延期」條件成立,不卡 M1 開發。
> rev 2026-07-15:§2 新增 quality 合成模型(off/const/focus_linked;q 與 focus 同源、σ_eff = σ₀/√q 掛鉤、q_null_th 掉樣),對應 SPEC-004 §3a.1 語意提案;測試 tag [sim][quality]。
> rev 2026-07-16:§7 開放問題 #4 改性質(EEPROM 假設格式轉正、Qualcomm 對齊改條件式);
> §3 測試對映新增 eeprom_equiv(block.json/txt 等價閉環)與五檔落盤 IT 補充。
> rev 2026-07-16(2):M1 驗收盤點補 FR-15 平滑性警告測試對映(原實作存在但無專屬案例)。

## 1. 測試層級

| 層級 | 對象 | 環境 |
|---|---|---|
| UT 單元 | core / io 各函式契約 | Catch2(C++),合成序列 |
| IT 整合 | Phase 串接(A→G,離線) | Sim 序列 dry-run |
| HT 硬體 | 真實模組 [M2] | 校正治具 |
| AT 驗收 | NFR + 產線情境 [M3] | 產線 |

## 2. 合成資料規格(Sim 序列生成器)

- 直接生成 `disp_seq.json`(SPEC-004 §3a),不經 pixel 域:
  - 真值注入:`true_dcc(r,c)`(中央 12.46,向角落線性升至 14.5)、
    合焦位置 **420 DAC**;`disp_true = (dac − 420) / true_dcc(r,c)`(raw_pixel)。
  - focus 序列:以 420 為峰之單峰凹曲線(如高斯/二次)生成 `fv`。
  - 可注入:disparity 高斯雜訊 σ_d、系統性 bias、無效樣本(null)、
    DAC 清單擾動/亂序、單位欄位錯置(供負向測試)。
  - 可指定輸出粒度(8×6 或細粒度如 144×108)以測試 D-5 聚合。
  - quality 面(SPEC-004 §3a.1 語意提案之示例來源;tag [sim][quality]):
    `off`(無 quality 面,預設)/ `const`(定值 1.0)/ `focus_linked` —
    `q = clamp(exp(−t²)·(1 − q_falloff·radial), 0, 1)`(t 與 focus 曲線同源);
    **噪聲掛鉤** σ_eff = σ₀/√max(q, 0.05) 使 q 誠實反映量測變異
    (M2 WLS/EIV fitter 之驗收基準);`q < q_null_th` → data 記 null
    (模擬外部門檻剔除,離焦端點先掉樣 → 運動 min_valid_samples/E-D03)。
- 無雜訊序列之解析真值:**k = true_dcc、b = 420、focus 峰值 = 420**
  (UT-05/06 之準則依此;交叉驗證誤差之非零情境由注入 bias 建構)。
- [M2 備忘] RAW 級合成器(chart 紋理、SimMotor 磁滯、SimCapture 飽和故障)
  移作外部 SAD 模組之驗證工具(§8 #4)。

## 3. 單元測試案例(對映 FR)

| ID | 對象 | 準則 |
|---|---|---|
| UT-01 | sweep(FR-03/04) | 端點 == 公式推導值(預設 margin 0.1 → 180/660;margin 0 → 220/620;margin 0.05 → 200/640)、10 點遞增、端點精確 |
| UT-02 | 序列讀取(FR-08) | 合法序列載入成功;形狀錯 → E-D01;DAC 不符/非遞增/unit 錯 → E-D02 |
| UT-03 | 單位轉換(FR-09) | 同一真值之 pd_image_grid 序列 ×16 後與 raw_pixel 序列結果 bit-exact 一致 |
| UT-04 | 無效樣本(FR-11) | null→NaN;單區有效 7 點 → E-D03;8 點通過 |
| UT-05 | regression(FR-12) | 無雜訊序列還原 k=12.46 / b=**420** 至 1e-6;負斜率 raise E-E01 |
| UT-06 | focus.peak(FR-13) | 無雜訊序列峰值 **420±1**;端點峰(合焦 > NEAR − step 之序列,預設 config 示例:合焦 640)→ E-F01 |
| UT-07 | validate(FR-14) | err 邊界值(0.199/0.200/0.201)判定正確 |
| UT-08 | eeprom(FR-16) | 12.46→0x031D;checksum 破壞可偵測;pack→read round-trip |
| UT-09 | 單位(SPEC-004 §3) | dcc_pd_grid / dcc_raw_px == pitch_x;對漏乘(≈1)/重複乘(≈pitch_x²)正確示警 |
| UT-10 | 粒度聚合(D-5) | 無雜訊細粒度(144×108)序列經 median 與 weighted_mean 聚合,結果皆與直接 8×6 序列一致(<1e-9);區內有效 cell 比例 0.49/0.50 邊界判定正確 |
| UT-補充 | eeprom_equiv(2026-07-16) | block.json/txt 與 `eeprom::pack()` 同源:encoded hex 與 `encode_q` 一致、checksum 與 pack 尾 byte 一致、layout bytes 總和 == block.bin 長度(993);假設模組基準值 12.46 → 0x031D 閉環 |
| UT-補充 | 平滑性警告(FR-15,2026-07-16) | 預設梯度(相鄰差 ~2-4%)於 smooth_limit 0.25 下不誤報;門檻壓至 0.01 觸發警告(含區座標),且模組判定仍 PASS(警告不 FAIL) |

## 3a. 誤差預算(外部 SAD 模組之 disparity 精度要求)

推導一律以 config 參數符號表述;數字為**預設 config 之推導示例**
(n=10、margin 0.1 → sweep span S = NEAR−FAR = 480、中央 DCC k=12.46、一般 OLS):

- DAC 離散度:`Sxx = h²·n(n²−1)/12`,`h = S/(n−1)`
  (示例:h≈53.33 → Sxx≈2.35×10⁵,√Sxx≈484)。
- disparity 離散度:`√Sdd = √Sxx / k`(示例 ≈ 38.9 raw px;
  全 sweep 動態範圍 `±S/2/k` ≈ ±19.3 raw px ≈ ±1.2 pd_image_grid)。
- 斜率相對誤差 ≈ `σ_d / √Sdd = σ_d · k / √Sxx`(σ_d = 單點 disparity 隨機誤差)。
- 各準則反推之 σ_d 上限(示例值):
  - NFR-02(CV < 2%)→ `σ_d ≤ 0.02·√Sxx/k` ≈ 0.78 raw px ← **綁定條件**
    (margin=0 時 S=400 → 收緊為 0.65,margin 越小要求越嚴)
  - IT-01(中央 DCC 誤差 < 3%,含系統項)→ 系統偏差另列預算
  - IT-01(r² > 0.99)→ `σ_d ≤ √(0.01·Sdd/(n−2))` ≈ 1.37 raw px(最鬆,非綁定)
- EIV 衰減偏差(噪聲在自變數):`≈ σ_d²·n/Sdd`,σ_d=0.78 時約 −0.4%,可忽略
  → 支持 v1 採一般 OLS(§8 備忘 #2)。

**驗收要求(固定值,對 margin ∈ [0, 0.2] 全域保守)**:於全 sweep 動態範圍內,
單點 disparity **|系統偏差| ≤ 0.3 raw px(≈0.019×pitch)、隨機 σ ≤ 0.5 raw px
(≈0.031×pitch)**——取 margin=0 之最嚴上限(0.65)再留裕度,margin 加大時裕度更寬。
(原 UT-03/04 之 0.35×/0.25×pitch(5.6/4 raw px)準則過鬆約一個數量級,作廢;
此要求轉為對外部 SAD 模組之介面驗收條件,並以 IT-07 於本工具端閉環驗證。)

## 4. 整合測試

| ID | 情境 | 準則 |
|---|---|---|
| IT-01 | 標準 dry-run(無雜訊序列) | PASS;中央 DCC 誤差 < 3%;全區 r² > 0.99 |
| IT-02 | 注入角落無效樣本(null) | 剔除生效;仍 PASS 或正確 FAIL(E-D03)於指定區 |
| IT-03 | 序列 disparity 取負(模擬 L/R 對調) | Phase E 以 E-E01 中止,現場資料落盤 |
| IT-04 | chart 距離錯(合焦 > NEAR − step,預設 config 示例 640) | E-F01 觸發 |
| IT-05 | [M2] 曝光過高幀 | Phase C 於該幀 E-C03 中止 |
| IT-06 | 離線重算 | 以落盤序列快照重跑,DCC 一致(bit-exact);dry-run 後 out_dir 五檔齊全(report.json、report.md、block.bin、block.json、block.txt) |
| IT-07 | 誤差預算閉環(§3a) | 注入 σ_d=0.5 px + bias=0.3 px,×10 次蒙地卡羅:中央 DCC CV < 2% 且偏差 < 3% |

## 5. 硬體/驗收測試(HT/AT)

- HT-01 重複性:同模組 ×10 次,中央 DCC CV < 2%(NFR-02)。
- HT-02 節拍:含上下料外之校正時間 ≤ 4 s(NFR-01)。
- HT-03 交叉驗證:抽樣模組以 CDAF 全掃描金標比對,對焦命中率
  (一步 PD 跳焦後 |殘餘| < 1 景深)≥ 95%。
- AT-01 產線 200 顆試跑:誤殺率(good 判 FAIL)< 2%、漏殺率以 CDAF 覆核 = 0。

## 6. 出場準則(規格 → 實作的 Gate)

- 本 SPEC 系列 review 簽核(校正/演算法/產線三方)。
- 開放問題清單(§7)全數關閉或明確延期。

## 7. 開放問題

> 2026-07-11 決議:以下各項均**不阻擋 M1 離線開發**;依 §6 出場準則以「明確延期」處理。

1. 真實 sensor 的 PD offsets 與「均勻子格」假設差異多大?
   —— **延期至 M2 前關閉**(等 vendor config;主要影響外部 SAD 模組,
   本工具僅 config 數值連動;介面品質由 §3a 誤差預算把關)
2. runtime PDLIB 實際 disparity 單位?
   —— **延期至 M2 實體燒錄前關閉**(設計已對沖:core 單一單位、
   `output_disparity_unit` 雙路徑皆實作並由 UT-09 覆蓋、report 雙單位記錄;
   定案時僅翻 config 值)
3. 產線治具之 chart 距離公差 ±?cm 對 DCC 的靈敏度
   —— **分析工具已落地(2026-07-12,M1c)**:GUI「靈敏度掃描」面板 +
   CLI `--scan`(CSV 輸出);synth 支援二階非線性注入 `nl`。
   已驗證:理想線性(nl=0)時 DCC 對合焦偏移完全不敏感;
   nl=0.05 時靈敏度 ≈ 2·nl·Δ/(span/2)(預設 span 480 → /240;+40 DAC → +1.7%,與理論一致)。
   **最終關閉待實模組量測真實非線性量級**(掃描方法與判讀已備妥)
4. EEPROM 實際 layout 與 v4 開發版差異(等 `PDAFCalibrationTools_EEPROM.h`)
   —— **改性質(2026-07-16)**:本專案非 Qualcomm 專用,開發版 layout 轉正;
   `PDAFCalibrationTools_EEPROM.h` 對齊為條件式(對接 Qualcomm 才需)。
5. ~~外部 SAD 模組輸出粒度~~ **已關閉(2026-07-11 決議)**:粒度可設定
   (序列自述 grid_w/grid_h,可 ≠ dcc.grid),聚合規則 median(預設)/
   quality 加權平均,config `aggregation.*`;規格見 SPEC-004 §3a、SPEC-002 D-5

## 8. 開發備忘(非阻擋規格凍結,隨里程碑處理)

1. **SAD 次像素精度風險**:全 sweep disparity 動態範圍僅 ±1〜1.2 pd_image_grid(依 margin),
   量測精度全靠次像素內插——屬外部 SAD 模組範疇,驗收依 §3a 誤差預算;
   chart 紋理空間頻率規格(避免與 16 px 取樣節拍共振)應納入外部模組規格。
2. **回歸方向 EIV 偏差**:`DAC = k·disp + b` 之 OLS 對含噪自變數有衰減偏差;
   §3a 評估在預算內可忽略(≈0.4%),v1 採一般 OLS;
   fitter 介面可抽換(SPEC-003 §5),精度需求提高時再換 EIV/反向擬合。
3. **鐵律 4 漸進嚴格化**:錯誤碼表滾動更新(SPEC-001 §3);
   開發初期允許通用碼與簡化落盤,M1 出口前收斂為具名碼 + 完整現場落盤。
4. **Pixel 級規格保留**:原 Phase D 抽取/增益/SAD 工序與 RAW 級合成器規格
   保留於 docs/,M2 對接外部 SAD 模組時作為驗證參考。

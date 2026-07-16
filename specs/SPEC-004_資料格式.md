# SPEC-004 · 資料格式規格

> rev 2026-07-11:修正 §3 ratio_check 公式;`disparity_unit` 拆分為 `input_/output_disparity_unit`;新增 §3a disparity 序列輸入格式(離線前期之主要輸入,含 focus value);schema 補齊各 Phase 可調參數;exposure 參數移列外部模組參考。
> rev 2026-07-11(2):§3a 支援 SAD 細粒度輸出與聚合設定(median/加權平均,關閉開放問題 #5);RAW 改列唯讀選配輸入(UI 底圖)。
> rev 2026-07-11(3):margin 預設定案 0.1;連動數值一律以公式參數化表述,範例值僅為預設 config 之推導示例。
> rev 2026-07-13:num_positions 由固定 10 改為可設定(5..50,預設 10),並明訂與 min_valid_samples / focus.poly_order / peak_margin_steps 之交叉約束(loader 於載入即檢核,E-A01)。
> rev 2026-07-15:新增 §3a.1 quality 數值語意**提案**(值域 [0,1]、σ 之單調遞減代理、建議標定 q ∝ 1/σ²),作為 M2 與外部 SAD 模組格式凍結之談判基準;Sim 端同步新增 focus_linked 合成模型(SPEC-005 §2)。
> rev 2026-07-16:disp_seq.json v1 與外部 SAD 團隊格式凍結(§3a);quality 語意改採對照表記錄對方標定(§3a.1);
> §4 EEPROM 假設格式轉正為正式輸出格式,`PDAFCalibrationTools_EEPROM.h` 對齊改為條件式;
> 新增 §4a 可讀等價檔 block.json / block.txt。

## 1. 檔案佈局

```
config/    sensor_config_*.json
data/disparity/<module_id>/disp_seq.json          # 外部 SAD 模組輸出(§3a),離線模式主要輸入
data/raw/<module_id>/frame_XX_dacYYYY.raw(+ .json metadata)   # 外部擷取模組提供;本工具唯讀
                                                   # (UI 影像底圖用,選配;不參與量測計算,不得寫入)
data/output/<module_id>/report.json / report.md / block.bin
```

## 2. Sensor Config JSON Schema(v1)

必填欄位與約束(範例見 `config/sensor_config_example.json`):

| 路徑 | 型別 | 約束 |
|---|---|---|
| sensor.width/height | int | 各能被 pattern_period 整除 |
| sensor.bit_depth | int | 10(本期) |
| sensor.black_level | int | 0..2^bit-1 |
| sensor.pattern_period_x/y | int | >0 |
| sensor.pd_left_offsets | [[x,y]×8] | 依 x 遞增;0≤x<period_x |
| sensor.pd_right_offsets | 同上 | 與 left 一一配對(同子格) |
| vcm.dac_min/max | int | min<max |
| vcm.af_cal_inf/macro | int | inf<macro,皆在 dac 範圍 |
| vcm.settle_time_ms | int | 預設 30 |
| dcc.far_margin/near_margin | float | 0..0.2,**預設 0.1**(sweep 端點由公式推導,見 FR-03;不得於規格/程式中寫死端點值) |
| dcc.num_positions | int | 5..50,**預設 10**;連動約束:min_valid_samples ≤ 本值、focus.poly_order < 本值、2×peak_margin_steps < 本值−1 |
| dcc.grid_w/grid_h | int | 8 / 6(v1) |
| dcc.q_format | int | 4..8,預設 6 |
| dcc.tolerance | float | metal-shield 預設 0.20 |
| dcc.input_disparity_unit | enum | "raw_pixel" \| "pd_image_grid";外部序列(§3a)之單位,讀取端轉 raw_pixel |
| dcc.output_disparity_unit | enum | "raw_pixel" \| "pd_image_grid";runtime PDLIB 期望單位,打包端轉換 |
| dcc.min_valid_samples | int | 預設 8(FR-11) |
| dcc.smooth_limit | float | 預設 0.25(FR-15) |
| dcc.r2_warn | float | 預設 0.98(Phase E 警告門檻) |
| focus.poly_order | int | 預設 4(Phase F 擬合階數) |
| focus.peak_margin_steps | int | 預設 1(FR-13 出界判定) |
| aggregation.method | enum | "median"(預設)\| "weighted_mean";SAD 細粒度 → 8×6 聚合(SPEC-002 D-5) |
| aggregation.min_valid_ratio | float | 預設 0.5;區內有效 cell 比例低於此 → 該區該幀無效 |
| exposure.pd_min/max_lsb | int | 300 / 950 [M2/外部參考:離線模式不使用] |

> 原則(FR-01):**所有 Phase 之可調參數皆須入本 schema**,不得散落於程式常數;
> v1 主格式 JSON,ini 為選配(鍵名一對一對映,如 `dcc.smooth_limit` ↔ `[dcc] smooth_limit`)。

## 3. 單位權威定義

- **disparity**:R 相對 L 之水平位移;右移為正。
  內部核心一律 raw_pixel;`pd_image_grid → raw_pixel` 乘 `pitch_x`。
- **DCC**:DAC / raw_pixel,無號正值。
- 單位轉換僅允許兩處:**讀取端**(io.disparity_reader,依 `input_disparity_unit`
  轉入 raw_pixel)與**打包端**(io.eeprom,依 `output_disparity_unit` 轉出);
  core 不得出現第二種單位(SPEC-003 §3)。
  report.json 必須同時記錄兩種單位之 DCC 以供比對稽核。
- **16 倍檢核**:report 內建欄位 `dcc_ratio_check = dcc_pd_grid / dcc_raw_px`,
  應恆等於 `pitch_x`(範例模組 = 16);
  若 ≈ 1(漏乘 pitch_x)或 ≈ pitch_x²(重複乘)即示警。

## 3a. Disparity 序列輸入格式(v1,已凍結 2026-07-16,離線模式主要輸入)

外部 SAD 模組每模組輸出一份 `disp_seq.json`(UTF-8):

| 欄位 | 型別 | 約束 |
|---|---|---|
| module_id | str | 與資料夾名一致 |
| unit | enum | "raw_pixel" \| "pd_image_grid";須等於 config `input_disparity_unit` |
| pitch_x | int | 須等於 config 推得之 pitch_x(單位驗算用,範例 16) |
| dacs | [int×N] | N == `dcc.num_positions`;嚴格遞增(FAR→NEAR,鐵律 3),與 Phase C-1 規劃比對 |
| grid_w / grid_h | int | 外部 SAD 之輸出粒度,**可 ≠ `dcc.grid`**(如 pattern block 級 144×108);≠ 時依 SPEC-002 D-5 聚合至 8×6 |
| data | [N][grid_h][grid_w] float\|null | disparity;無效樣本以 null |
| focus | [N][grid_h][grid_w] float | 逐 cell focus value(梯度能量等;Phase F 僅用峰值位置,量綱不拘但同序列內須一致;與 disparity 同規則聚合)|
| quality | 同 data shape(選填) | 品質指標(數值語意見 §3a.1;門檻判定於外部完成;data 為 null 即無效;`weighted_mean` 聚合時作權重,缺則等權)|
| meta(選填) | dict | 曝光、溫度、治具 ID 等追溯資訊 |

讀取驗證規則見 SPEC-002 Phase D(E-D01/E-D02);讀取端即轉為 raw_pixel;
粒度聚合(median / quality 加權平均,config `aggregation.*`)見 SPEC-002 D-5。

### 3a.1 quality 數值語意(已凍結 2026-07-16)

> **凍結記錄(2026-07-16)**:disp_seq.json v1 欄位結構/單位/必填規則已與外部 SAD 團隊凍結。
> quality 數值語意**未採**本節原提案(q ∝ 1/σ²),對方標定為 **SAD 曲線形狀導出
> (極小值尖銳度/峰谷比類)**;具體公式與值域文件待補,補齊後更新下表。
>
> | 面向 | 本工具原提案 | 對方凍結標定 |
> |---|---|---|
> | 來源 | σ 單調遞減代理,q ∝ 1/σ² | SAD 極小值尖銳度/峰谷比 |
> | 對 D-5 聚合 | 最小變異加權 | 單調信心權重(仍可用) |
> | 對 M2 WLS/EIV | q 可直接推 σ | **須先建立 q→σ 標定才可作權重** |

原提案內容:

- **值域 [0,1]**,float;`1.0` = 最高信心、`0.0` = 不可信。
- **語意**:該 cell disparity 量測標準差 σ 的**單調遞減代理**;
  建議標定 **q ∝ 1/σ²**(資訊權重)——如此 `weighted_mean` 聚合即為最小變異加權,
  M2 之 WLS/EIV fitter(SPEC-005 §8 #2)可直接以 q 推每樣本權重/σ。
- **跨幀跨區可比**:同一序列內須同一標定,**不得逐幀重正規化**
  (否則權重跨幀失義,聚合與加權回歸皆被破壞)。
- **門檻剔除於外部完成**:低於外部信心門檻者應直接輸出 `data = null`,
  而非僅給低 q(本工具不做門檻判定;q = 0 之 cell 於 `weighted_mean` 權重為零,效果等同剔除)。
- **本工具消費點**(現況):僅 SPEC-002 D-5 聚合(序列 grid ≠ `dcc.grid` 時);
  grid 相同時 D-5 跳過,quality 僅原樣進 report 供追溯。
- **物理直覺**(供外部對齊):SAD 信心與紋理對比正相關——離焦越大、影像越糊、
  視差越不可靠;離軸(MTF 下降/vignette)亦然。Sim 端之 `focus_linked` 模型
  (SPEC-005 §2)即以此語意產生示例資料:q 與 focus 曲線同源、含徑向衰減,
  且合成噪聲 σ_eff = σ₀/√q 與 q 自洽(「誠實」原則),作為 M2 fitter 之驗收基準。

## 4. 校正 Block(v4 精神,開發版 layout)

| offset | bytes | 內容 | 編碼 |
|---|---|---|---|
| 0x0000 | 2 | version = 4 | uint16 BE |
| 0x0002 | 4 | gain W, H | uint16×2 |
| 0x0006 | 442 | Left Gain[221] | Q10, uint16 BE |
| 0x01C0 | 442 | Right Gain[221] | Q10 |
| 0x037A | 2 | DCC Q format | uint16 |
| 0x037C | 4 | DCC W, H | uint16×2 |
| 0x0380 | 96 | DCC[48] | Q(q), uint16 BE |
| 0x03E0 | 1 | checksum = Σ前段 %256 | uint8 |

> 本專案非 Qualcomm 專用(2026-07-16 決議):本開發版 layout 即正式輸出格式;
> `PDAFCalibrationTools_EEPROM.h` 逐位對齊改為**條件式**——僅對接 Qualcomm 平台時執行,差異以對照表記錄。
> 離線前期:pack/verify 落盤 `block.bin` 並經 SimNvm 回讀驗證;實體燒錄屬 M2。

### 4a. 可讀等價檔 block.json / block.txt(2026-07-16 新增)

與 `block.bin` 同目錄輸出、同源生成(相同 pack 輸入),供跨平台整合與人工核對:

- `block.json`:`format="dcc-block-equiv v1"`;含 block_version、module_id、config_hash、
  `layout[]`(每欄 field/offset/bytes/encoding)、`total_bytes`、
  gain(w/h/left/right 物理值)、dcc(w/h/unit/q_format/values 物理值/encoded_hex)、
  checksum(value/hex/rule)。**確定性輸出,無時間戳**(同 §5 report.json 準則)。
- `block.txt`:人閱讀版(繁中);layout 表 + gain 表 + DCC 物理值/hex 兩張 8×6 表 + checksum。
- 等價性守則:`total_bytes` 與 `checksum` 由 `eeprom::pack()` 同源取得,
  layout bytes 總和 == block.bin 長度(993),由 UT 閉環。

## 5. report.json Schema(v1,節錄)

```
{ module_id, ts, config_snapshot, sweep:{dacs[10]},
  disparity:{unit,"data"[10][6][8], quality[10][6][8]},
  focus:{fv[10][6][8], peak[6][8]},
  result:{dcc_raw_px[6][8], dcc_pd_grid[6][8], intercept[6][8],
          r2[6][8], err[6][8], pass:bool, worst:[{r,c,err}×3]},
  errors:[{code,msg,phase}], timing_ms:{per_phase} }
```

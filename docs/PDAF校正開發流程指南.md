# PDAF 相位自動對焦 — 校正與開發流程指南

> 主要依據:Qualcomm《PDAF Module Calibration Guide》(80-NV125-1 Rev. L5, 2017)
> 適用對象:相機模組校正 / AF 演算法開發工程師

---

## 1. 系統架構與資料流

```
PD pixels ──► Gain 校正 ──► Phase Disparity 計算 ──► Phase→Defocus 轉換 ──► PDLIB ──► (輔以 Contrast AF 收斂)
                 ▲                                        ▲
            GAIN MAP                                  DCC MAP
           (來自校正)                                 (來自校正)
```

PDLIB(Phase Detection Library)負責估算鏡頭離焦量。要正確算出 phase disparity
並轉換成鏡頭移動量(DAC code),必須依賴兩份校正資料:**Gain Map** 與 **DCC Map**。

---

## 2. 校正總覽:2 個校正步驟 + 1 個前置設定

| # | 項目 | 性質 | 產出 |
|---|------|------|------|
| 0 | Sensor Configuration | 前置設定 | PD 像素座標 / pattern 設定檔 |
| 1 | Gain Map 校正(SPC) | 逐模組校正 | 左 / 右 gain map(13×17) |
| 2 | DCC 校正 | 逐模組校正 | DCC map(8×6,DAC/pixel) |

**先後順序限制:**
- OIS 模組:先做 **OIS 校正**,確保鏡頭停在參考 XY 位置,再做 LSC 與 gain map 校正。
- **AF 校正必須先於 DCC 校正**,因為 DCC 需要 `AF_CAL_INF` 與 `AF_CAL_MACRO` 鏡頭位置。

---

## 3. 前置:Sensor Configuration

- 對 sparse PDAF(PD 像素以重複 pattern 分佈)的感光元件,需指定 PD 像素位置,
  校正 DLL 才能從 RAW 影像正確解析 PD 像素。
- 設定檔由 sensor 廠商提供(例:Sony IMX258 的 sensor configuration file),
  但必須逐項核對實際 RAW 輸出(影像尺寸、PD 像素區塊位置可能與範例檔不同)。
- PD 像素座標須依 x 座標遞增排序(DLL 不做自動排序)。
- RAW 影像必須以 **canonical orientation** 讀出:flip 與 mirror 都要關閉。

---

## 4. 校正步驟一:Gain Map(SPC)

### 目的
PD 像素(遮光 / 分光結構)靈敏度低於一般像素,需以平場(flat-field)影像
建立左 / 右 gain map,補償增益後才能準確計算 phase disparity。

### 測試場景設定
| 參數 | 設定值 |
|------|--------|
| 鏡頭位置 | `AF_CAL_INF` 與 `AF_CAL_MACRO` 的中點 |
| 光源 | D50 或 D65(與 LSC 校正相同光源) |
| Sensor gain | Metal-shield / 2x1 OCL:analog=digital=1X;Dual-PD:**analog=2X**、digital=1X |
| 曝光 | 中央 10%×10% ROI 內,左右 PD 像素平均值介於 100–950 LSB(10-bit) |
| Frame average | 與 LSC 校正相同的平均張數 |

### 關鍵陷阱:Blooming(電荷溢出)
- Dual-PD 感光元件在 sensor gain 1X–2X 之間,左右光電二極體間可能發生 blooming,
  使平場響應變形、gain map 失真。**解法:analog gain 設 2X**,讓 blooming 門檻貼近 ADC 上限。
- Metal-shield 型:一般像素靈敏度遠高於遮光像素,需確保一般像素不飽和,
  避免電荷溢入遮光像素。

### 流程
1. 架設平場測試場景(如上表)。
2. OIS 模組先完成 OIS 校正。
3. 拍攝平場影像:
   - Sparse PDAF:全解析度 RAW(由 DLL 解析 PD 像素)。
   - Dual-PD:全解析度 RAW 或 tail mode buffer(如 IMX362 Mode 4;
     buffer 必須先解析成左右影像對才能送入 DLL)。
4. 呼叫 `PDAF_Cal_get_gainmap(...)`(Dual-PD 用 `PDAF_Cal_get_gainmap_2pd(...)`)。
   輸入含影像寬高、black level、bit depth;若 sensor 有 flip/mirror 需先還原。
5. 檢查回傳碼(像素飽和、gain map 異常等);有錯即中止,勿燒錄無效資料。
6. 將 gain map 寫入 NVM/OTP。

> 產出的 gain map 固定為 13×17(高×寬);NVM/OTP 格式支援可變尺寸。
> Dual-PD 的 phase 計算只用 2×2 binned 綠色像素;tail mode 若只輸出 Y,
> 需把 R、B 權重設 0 使 Y = G。

---

## 5. 校正步驟二:DCC(Defocus Conversion Coefficient)

### 定義
```
DCC = Δ鏡頭位置 [DAC] / Δphase disparity [pixel]    (單位:DAC/pixel)
```
- QTI 定義下 DCC 為 **無正負號**、存為正值;工具回報負值即校正無效
  (通常是 sensor config 的 LEFT/RIGHT 標記顛倒)。
- 因鏡頭場曲與 shading,周邊與中央的 k 值不同(周邊通常較高),
  故以區塊化 DCC map 表達,而非單一係數。

### 測試場景設定
| 參數 | 設定值 |
|------|--------|
| Chart | 垂直線 chart(強烈建議)或 diamond chart |
| Chart 距離 | 對應「INF 與 MACRO 中點鏡頭位置」的物距,一般 20–30 cm(望遠鏡頭可達 2 m) |
| Chart 尺寸 | 相機 FOV 覆蓋 chart 有效區的 85%–95% |
| 光源 | ≥400 lux,D50/D65 |
| Sensor gain | 同 gain map 校正 |
| 曝光 | Chart 白底對應的中央 10%×10% ROI,左右 PD 平均值 300–950 LSB |

**常見錯誤:** chart 太小 / 太大、旋轉、傾斜、過曝 — 都會導致系統性 DCC 誤差。

### 鏡頭掃描範圍
```
FAR_LP_LIMIT  = AF_CAL_INF   − (MACRO − INF) × DCC_LP_FAR_MARGIN
NEAR_LP_LIMIT = AF_CAL_MACRO + (MACRO − INF) × DCC_LP_NEAR_MARGIN
LP_STEP_SIZE  = |NEAR − FAR| / 9
```
- 非望遠鏡頭:margin 預設 0.0。
- 望遠鏡頭:margin 建議 0.10;場曲超過 10% 的鏡頭應在 DCC 校正前篩除
  (角落 INF/MACRO 位置與中央差異 > 0.10 × |MACRO − INF| 即淘汰),
  否則會觸發 `0x2000: Focus peak is out of boundary`。

### 流程
1. 架設 chart 場景(如上表);AF 校正須已完成。
2. 鏡頭移到 `FAR_LP_LIMIT`,拍第 1 張 RAW,
   呼叫 `PDAF_Cal_add_raw(...)`(Dual-PD 用 `PDAF_Cal_add_raw_2pd(...)`)。
3. 每次朝 `NEAR_LP_LIMIT` 移動 1/9 行程,待鏡頭穩定後拍照並入堆疊。
4. 重複至第 10 張(共 10 個鏡頭位置 → 10 組 phase disparity + focus value)。
5. 呼叫 `PDAF_Cal_get_dccmap(...)`:
   - 影像切成 **6×8 區域**,各區以線性回歸求 DCC → 8×6 DCC map。
6. 檢查回傳碼;有錯即中止並查根因。
7. 將 DCC map 寫入 NVM/OTP。

### 驗證(整合於校正流程)
- 掃描期間同時計算 focus value,以多項式回歸求「真實」峰值鏡頭位置。
- DCC 誤差 = |phase disparity 直線 y 截距推得的位置 − focus 峰值位置|,
  再以 FAR–NEAR 行程正規化。
- 預設容差:Metal-shield **20%**、2x1 OCL **15%**、Dual-PD **10%**
  (`DCC_VAL_TOL_SPARSE / _2BY1 / _DPD`,同為 8×6 grid,勿隨意放寬)。
- 健康的 DCC map 應為平滑曲面;出現大量局部極值即為校正異常。

---

## 6. 校正資料格式(OTP/EEPROM, Version 4)

以 `PDAF_Cal_get_calibration_block(...)` 取得燒錄 buffer,
格式定義於 `PDAFCalibrationTools_EEPROM.h`:

| 位址 | 內容 |
|------|------|
| 0x0000–0x0001 | 版本號 |
| 0x0002–0x0005 | Gain Map 寬 / 高 |
| 0x0006–0x01BF | Left GainMap[0..220](各 2 bytes, H/L) |
| 0x01C0–0x0379 | Right GainMap[0..220] |
| 0x037A–0x037B | DCC Q Format |
| 0x037C–0x037F | DCC Map 寬 / 高 |
| 0x0380–0x03DF | DccMap[0..47] |
| 0x03E0 | Checksum %256 |

### 主要驗證參數(Chapter 4)
- `pd_max_limit`(建議 950)/ `pd_min_limit`(建議 100):平場 LPF 後 PD 像素上下限。
- `gain_max_limit`:gain 上限,不得超過 7.999(工具限制)。

---

## 7. 演算法開發者 Checklist

- [ ] 取得並核對 sensor configuration(PD 座標排序、canonical orientation)
- [ ] 確認 sensor 類型:metal-shield / 2x1 OCL / dual-PD(影響 gain、API、容差)
- [ ] OIS → AF → LSC/Gain Map → DCC 的校正順序正確
- [ ] Gain map 校正時檢查 blooming 與飽和(dual-PD analog gain = 2X)
- [ ] DCC chart 距離 / 尺寸 / 姿態 / 曝光符合規範
- [ ] DCC 為正值(負值 = LEFT/RIGHT 顛倒)
- [ ] DCC map 平滑、驗證誤差在容差內
- [ ] EEPROM 資料含 checksum,燒錄前先驗證回讀
- [ ] Runtime 端:PDLIB 讀入 gain map 先做 PD 增益補償,再算 disparity,
      最後用 per-region DCC 做 phase→defocus 轉換;殘餘誤差交由 Contrast AF 微調

---

## 8. 參考資料

- Qualcomm, *PDAF Module Calibration Guide*, 80-NV125-1 Rev. L5
  https://usermanual.wiki/Pdf/80NV1251PDAFModuleCalibrationGuide.750511322/help
- US10387477 / US20180349378A1 — Calibration for PDAF camera systems(Qualcomm 專利)
- US10070042 — Method and system of self-calibration for PDAF(自校正方向)
- CN106556960B — DCC 驗證方法(以 CDAF 對照驗證 DCC 的產線手法)

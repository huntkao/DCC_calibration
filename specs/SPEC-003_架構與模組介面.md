# SPEC-003 · 軟體架構與模組介面規格

> rev 2026-07-11:新增 io.disparity_reader(離線主要輸入);pd_extract/disparity 契約移轉外部模組;hal 前期僅 Sim 實作;regression fitter 明訂可抽換。
> rev 2026-07-11(2):新增 io.raw_reader(RAW 唯讀,UI 底圖);disparity_io 納入粒度聚合;tools 層明訂互動式 UI。
> rev 2026-07-12:M0 凍結後之實作決議勘誤——實作語言 C++17;分層改為 C++ 模組(新增 dcc_app 層);前端技術選型:GUI = Dear ImGui(docking)+ ImPlot + ImGuiTexInspect(vendored),測試 = Catch2。
> rev 2026-07-17:§5 regression fitter 由「可抽換」落地為可組態(enum+config),新增 core qsigma 模組。

> 本文件定義「介面契約」——簽名、單位、前後置條件;不含實作。

## 1. 分層(C++17)

```
┌─ dcc_gui/   ImGui 桌面前端 ─┐ 僅:發命令、讀快照、繪製
├─ dcc_cli/   CLI 前端       ─┘(兩前端同源、能力等價)
├─ dcc_app/   SessionController:Phase 狀態機、config 失效傳播、
│             背景重算(可取消)、不可變快照發布
├─ dcc_io/    config / log / 報告 / EEPROM / disp_seq 讀取 / RAW 唯讀
├─ dcc_sim/   合成資料生成(獨立 library;與管線平行之「生產者」,
│             產出與外部 SAD 模組同格式之序列;GUI Sim 工作台/靈敏度掃描後端)
├─ dcc_hal/   硬體抽象(馬達、擷取、NVM)——介面注入;前期僅 Sim* 實作
└─ dcc_core/  純演算法(無 I/O、無 UI、無執行緒;可離線重算)
```

規則:依賴方向嚴格單向 gui/cli → app → io/hal → core;
dcc_core 不依賴任何其他層;所有單位換算集中在 core units(§3)。
UI 與演算法完全解耦:GUI 做得到的操作,CLI 必須也做得到(解耦試金石)。

## 2. 核心資料結構(語意定義)

| 名稱 | 形狀/型別 | 語意 |
|---|---|---|
| `DispSeq`  | dict(schema 見 SPEC-004 §3a) | 外部模組之 disparity/focus 序列(離線主要輸入)|
| `DispGrid` | float64 (6,8) | 單張之逐區 disparity(單位見 §3) |
| `DccMap`   | float64 (6,8) | 逐區斜率,恆正 |
| `Report`   | dict(schema 見 SPEC-004 §5) | 單模組結果 |
| `RawFrame` | uint16 (H,W) + meta{dac,exp,gain,ts} | 外部擷取之單張全幅 RAW;唯讀載入(io.raw_reader)供 UI 底圖,不入 core 計算路徑 |
| `PdImage`  | float32 (H/pitch_y, W/pitch_x) | [M2/外部] 抽取+去黑位後之 L 或 R(外部模組領域,參考用)|

## 3. 單位契約(全案最高優先)

- 內部一律使用 **raw_pixel** 作 disparity 單位;PD 影像格(pd_image_grid)
  僅允許存在於外部序列輸入,**讀取端**(io.disparity_reader)依 config
  `input_disparity_unit` 於入 core 前乘 `pitch_x` 轉為 raw_pixel。
- `DCC` 單位固定 **DAC / raw_pixel**。
- 若 runtime PDLIB 使用 PD 影像格,轉換責任在**輸出端**(io.eeprom 打包前
  依 config `output_disparity_unit` 決定是否除以 pitch_x),core 不得出現第二種單位。
- 單位轉換全案僅允許上述兩處(讀取端 / 打包端)。
- 每個跨模組函式之 docstring 首行必須標注輸入/輸出單位(review 檢查項)。

## 4. HAL 介面

```
MotorIF:
  move(dac:int) -> None          # 阻塞至命令送達;不含 settle
  position() -> int              # 最後命令值(除錯用)
CaptureIF:
  grab() -> RawFrame             # 阻塞;逾時 raise CaptureTimeout
NvmIF:
  read_gain_maps() -> (GainMap13x17, GainMap13x17)
  write_block(blob: bytes) -> None
  read_block(n: int) -> bytes
```
合成模式提供 `SimMotor / SimCapture / SimNvm`(SPEC-005 §2)。
**前期僅實作 Sim*;真實 HAL 屬 M2,介面不變。**

## 5. core / io 函式契約(節錄)

```
sweep.plan(vcm, dcc) -> list[int]
  post: len==num_positions(預設 10), 遞增, 端點==FAR/NEAR(取整後), 全在 DAC 範圍

disparity_io.load(path, cfg) -> (disp[10,6,8], quality[10,6,8], fv[10,6,8], dacs[10])
  out : disp 一律 raw_pixel(讀取端已依 input_disparity_unit 轉換);無效樣本 NaN;
        序列 grid ≠ dcc.grid 時已依 cfg aggregation.* 聚合(SPEC-002 D-5)
  post: SPEC-002 Phase D 之 D-1..D-6 驗證全過;失敗 raise SeqError(E-D01/E-D02/E-D03)

raw_io.load(path) -> RawFrame
  唯讀;僅供 tools 層 UI 顯示,禁止進入 core 計算路徑;缺檔回 None(降級顯示)

regression.fit(dacs[10], disp[10,6,8]) -> (DccMap, InterceptMap, R2Map)
  pre : 每區非 NaN 樣本 >= min_valid_samples;post: DccMap>0 否則 RegressionSignError(E-E01)
  note: fitter 可組態(config regression.fitter):v1 預設 OLS;另有 ols_inverse/
        wls_inverse(w=q^γ)/deming——實驗結論見 docs/fitter實驗_反向WLS與EIV.md

focus.peak(dacs[10], fv[10]) -> float
  post: 峰值嚴格在 (min+1step, max-1step) 內,否則 FocusBoundaryError(E-F01)

validate.judge(intercept, peaks, span, tol) -> (bool, ErrMap)

eeprom.pack(gainL, gainR, dcc, q) -> bytes;eeprom.verify(blob) -> bool
```

## 5a. dcc_app 契約(SessionController,參數一致性之守門)

```
SessionController:
  set_config(cfg) -> None      # 產生新版本+hash;依失效傳播圖將下游 Phase 標 dirty
  run(phase | all) / cancel()  # 背景執行緒重算;可取消
  snapshot() -> ResultSnapshot # 不可變快照(附 config_hash);dirty 期間前端須顯示 stale
```
- 每個 Phase 產物攜帶「產生它的 config_hash」;快照為原子性發布,
  前端永遠不會讀到新舊參數混合之結果(計算一致性保證)。
- report 落盤必含 config 快照 + hash → 離線重算 bit-exact(IT-06)之依據。

## 6. 錯誤處理策略
- core 以具名例外(對映錯誤碼)上拋;tools 層轉為操作員訊息 + log。
- 任何中止都必須先落盤:已擷取 RAW(可設定)、DispGrid、參數快照。

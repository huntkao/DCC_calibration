# 設計:EEPROM 可讀等價輸出 + 格式凍結記錄(2026-07-16)

## 背景與決議

- 外部 SAD 團隊之 `disp_seq.json` v1 **格式已凍結**(2026-07-16)。
  quality 數值語意**未採**本工具端 §3a.1 提案(q ∝ 1/σ²),對方標定為
  **SAD 曲線形狀導出**(極小值尖銳度/峰谷比類);具體公式與值域文件待補。
- 本專案**非 Qualcomm 專用**:EEPROM 假設格式(SPEC-004 §4 開發版 v4 layout)
  即為本工具正式輸出格式;`PDAFCalibrationTools_EEPROM.h` 逐位對齊降級為
  「僅對接 Qualcomm 平台時才需」。
- 為未來跨平台整合通用性,`block.bin` 之外**另輸出可讀等價資料檔**
  `block.json` 與 `block.txt`。

## 範圍(三件事)

### 1. 可讀等價檔(新功能)

- 落點:與 `block.bin` 同一 out_dir,檔名 `block.json`、`block.txt`。
- 內容來源:**由計算結果直接輸出**(與 `pack()` 相同輸入),不經 unpack round-trip。
- 內容(兩檔等價,json 給機器、txt 給人眼):
  - meta:block version、q_format、DCC 單位(依 `output_disparity_unit`)、
    module_id、時間戳、config hash;
  - layout 對應:每欄位含 offset、bytes、編碼方式(對應 SPEC-004 §4 表);
  - 資料:gain L/R(物理值)、DCC 8×6(**物理值與 Q 編碼 raw 值 hex 並列**,
    如 12.46 → 0x031D);
  - checksum(與 block.bin 尾 byte 同值)。
- txt 排版:DCC 以 8×6 表格呈現,物理值與 hex 兩張表。

### 2. Spec 勘誤

- SPEC-004 §3a:「待凍結」→「已凍結(2026-07-16)」。
- SPEC-004 §3a.1:加對照表——對方 quality 為 SAD 曲線形狀導出、非 σ 代理;
  註明 **M2 WLS/EIV fitter 以 q 作權重前須先建立 q→σ 標定**;
  現行消費點(D-5 聚合單調權重、report 追溯)不受影響。
- SPEC-004 §4:註明假設格式即正式輸出格式;`PDAFCalibrationTools_EEPROM.h`
  對齊改為條件式(對接 Qualcomm 才需);同步新增 block.json/txt 之 schema 節。
- SPEC-005:開放問題 #4 改性質(§7);新增可讀等價檔之 UT/IT 對映。

### 3. 開發紀錄

- `docs/開發紀錄_M0-M1.md`:M2 待辦 #1 標完成(附凍結摘要)、#3 改寫
  (EEPROM 假設格式轉正 + 等價檔)、#4 之前提條件更新。

## 實作位置(A 案)

新增 `src/dcc_io/include/dcc_io/eeprom_equiv.hpp` + `src/dcc_io/src/eeprom_equiv.cpp`:

```
// 輸入單位:dcc 已依 output_disparity_unit 轉換(與 eeprom::pack 同);gain 線性增益。
std::string build_block_json(meta..., gain_l, gain_r, gain_w, gain_h,
                             dcc, dcc_w, dcc_h, q_format);
std::string build_block_txt(同上);
```

- 編碼 raw 值以 `dcc_core::eeprom::encode_q` 計算(單一真相來源)。
- `dcc_app/session.cpp` 與 `dcc_gui/panels.cpp` 共同呼叫;
  順帶把兩處各自落盤 block.bin 的重複收斂為共用路徑。
- 分層:gui/cli → app → io → core,不新增反向依賴;core 不引入 json 依賴。

## 測試(先寫測試)

- UT:json 可解析且欄位值與 pack 輸入一致;encoded hex 與 `encode_q` 一致;
  checksum 與 `pack()` 輸出尾 byte 一致;假設模組基準值(12.46 → 0x031D)閉環。
- IT:dry-run 後 out_dir 五檔齊全(report.json、report.md、block.bin、
  block.json、block.txt)。
- 驗算文化:block.json 內各欄位 bytes 總和 = block.bin 實際長度(0x03E1 = 993)。

## 錯誤處理

- 等價檔寫出失敗不得影響 block.bin 已落盤結果;沿用 session 現有檔案寫出錯誤路徑。
- encode_q 出界仍走既有 E-G01(先於等價檔生成觸發)。

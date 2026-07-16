# dcc_seqview — 獨立 disp_seq.json 檢視器

給**外部 SAD 模組團隊**用的輕量工具:離線瀏覽/初步自檢自己產出的
`disp_seq.json`(格式定義見 [`../../specs/SPEC-004_資料格式.md`](../../specs/SPEC-004_資料格式.md) §3a),
在送交 DCC 校正工具前先確認結構正確、逐區資料/focus 曲線/quality 面長相合理。

**與校正引擎完全獨立**:本工具零依賴 `dcc_core`/`dcc_io`/`dcc_app`/`dcc_sim`,
只需 `nlohmann_json`(解析)與 `dcc_gui_common`(共用 ImGui/ImPlot 主題,
與 `dcc_gui` 保持介面一致)。顯示原始粒度(不做 D-5 聚合、不做單位轉換、
不跑校正計算)——純粹瀏覽與結構自檢,診斷邏輯故意比管線寬鬆:能收集到的
問題都會列在「診斷」面板,而不是抓到第一個錯誤就中止。

## 建置

只需整個 repo(依賴皆 CMake FetchContent 鎖版,無需另外安裝):

```bash
cmake -S . -B build -G Ninja
cmake --build build --target dcc_seqview
```

不需要的話可用 `-DDCC_BUILD_SEQVIEW_GUI=OFF` 關閉(預設 ON)。

## 使用

```bash
./build/src/dcc_seqview/dcc_seqview path/to/disp_seq.json   # 開檔啟動
./build/src/dcc_seqview/dcc_seqview                          # 空啟動,GUI 內「開啟檔案」面板再輸入路徑
./build/src/dcc_seqview/dcc_seqview --smoke                  # 建置驗證用(隱藏視窗渲染 5 幀)
```

面板:「開啟檔案」(路徑 + 摘要資訊)、「診斷」(issue 清單,error/warn 分色)、
「區域總覽」(逐區有效樣本數 heatmap,點擊選取)、「區域檢視」(該區
disparity/focus/quality vs DAC 三張圖,採樣點藍色標示)。

路徑欄位留空時按「開啟」會跳出原生檔案選擇視窗(`tinyfiledialogs`,
macOS/Windows/Linux 皆走各平台既有工具,無需額外安裝);若欄位已有內容則直接用該路徑開檔。

## 與管線讀取器(`dcc_io::disp_seq_reader`)的差異

| | `dcc_seqview` | `dcc_io::disp_seq_reader` |
|---|---|---|
| 定位 | 產出者自檢工具 | 管線入口閘門 |
| 粒度 | 原始(不聚合) | 聚合至 `dcc.grid`(D-5) |
| 遇到問題 | 收集所有 issue,仍可瀏覽 | 擋到第一個違規即中止(E-D01/02/03) |
| 需要 config | 不需要 | 需要(單位/pitch/grid 比對) |

兩者刻意分開:後者是「能不能進管線」的判定,前者是「送出前先看一眼哪裡怪」的輔助。

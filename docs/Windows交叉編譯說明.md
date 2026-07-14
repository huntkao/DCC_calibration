# Windows 交叉編譯(mingw-w64,全靜態連結)

> 建立日期 2026-07-14。於 macOS 上交叉編譯 `dcc_cal.exe` / `dcc_gui.exe`,
> 目標:**完整連結、無第三方執行階段依賴庫**(libstdc++-6.dll、
> libgcc_s_seh-1.dll、libwinpthread-1.dll 皆不需要)。

## 環境

```bash
brew install mingw-w64      # 提供 x86_64-w64-mingw32-gcc/g++,GCC 15.2.0,posix thread model
```

## 建置

```bash
cmake -S . -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DDCC_BUILD_GUI=ON -DDCC_BUILD_TESTS=OFF
cmake --build build-win
x86_64-w64-mingw32-strip build-win/src/dcc_cli/dcc_cal.exe build-win/src/dcc_gui/dcc_gui.exe
```

`DCC_BUILD_TESTS` 於交叉編譯時自動關閉(CMakeLists.txt 判斷 `CMAKE_CROSSCOMPILING`)——
Windows 版 Catch2 測試執行檔在 macOS 上無法執行,驗證仍以原生 macOS build 的
`ctest`(58 案例)為準;兩平台共用同一份原始碼與 CMake 目標定義。

## 工具鏈檔重點(`cmake/toolchain-mingw64.cmake`)

- `CMAKE_EXE_LINKER_FLAGS_INIT = "-static -static-libgcc -static-libstdc++"`
  —— 將 libgcc、libstdc++、winpthread(posix thread model 下之執行緒實作)
  全部靜態連結進執行檔。
- GLFW/OpenGL 於 Windows 走 Win32 + WGL 原生後端(`find_package(OpenGL)`
  自動找到 `opengl32`),不需額外第三方繪圖庫。

## 驗證結果

**依賴清單(`objdump -p <exe> | grep "DLL Name"`)**——兩檔皆僅剩 Windows
系統本身之 DLL,無任何第三方/MinGW 執行階段 DLL:

| 執行檔 | 依賴 DLL |
|---|---|
| `dcc_cal.exe` | KERNEL32、`api-ms-win-crt-*`(UCRT,Windows 10+ 內建) |
| `dcc_gui.exe` | 上列 + GDI32、USER32、SHELL32、OPENGL32 |

**符號解析**:`nm -u`(未定義符號)過濾掉正常的 DLL import thunk
(`_imp_*`)後為**零筆**——確認連結階段無任何應用層符號缺漏
(ninja 連結步驟本身若有缺漏會直接失敗,此為雙重確認)。

**檔案大小**(strip 後):`dcc_cal.exe` 約 1.5 MB、`dcc_gui.exe` 約 5.7 MB。

**未驗證項目(誠實揭露)**:未在 Windows 或 Wine 上實際執行——安裝 Wine
在 Apple Silicon macOS 上需連帶 Rosetta/XQuartz 等系統層變更,超出本次
任務範圍且非必要(靜態連結目標已由依賴清單 + 符號解析雙重證實)。
若需實機驗證,建議直接在目標 Windows 機器上執行
`dcc_cal.exe --dry-run` 確認 CLI 路徑,並開啟 `dcc_gui.exe` 確認視窗渲染。

## 已修正的可攜性問題

`src/dcc_sim/include/dcc_sim/synth.hpp` 使用 `std::uint16_t` 卻缺少
`#include <cstdint>`——macOS 的 AppleClang libc++ 標頭鏈間接引入,
mingw 的 libstdc++ 標頭鏈未間接引入,交叉編譯時報錯
`'uint16_t' is not a member of 'std'`。已補上 include(commit 隨附)。
全專案已掃描確認無其他同類缺漏。

## 已知取捨:`dcc_gui.exe` 為 console 子系統

目前連結為 console subsystem(與 macOS 版行為一致):執行時會在主視窗
背後多開一個主控台。這是刻意選擇——保留 `stdout`/`--smoke` 輸出能力
(此專案驗證流程仰賴它)。若要改為純 GUI 子系統(雙擊執行不跳主控台),
於 `src/dcc_gui/CMakeLists.txt` 對 `dcc_gui` target 加
`target_link_options(dcc_gui PRIVATE -mwindows)` 即可,但屆時
`--smoke`/一般 log 的 stdout 輸出將不可見(需另接 `AllocConsole`/
檔案 log——本專案已有 `dcc_io::Logger` 落盤,可作為替代管道)。

## 重現性

工具鏈檔已入版控(`cmake/toolchain-mingw64.cmake`),任何裝有
`mingw-w64` 的 macOS/Linux 機器皆可重現上述建置與驗證步驟。

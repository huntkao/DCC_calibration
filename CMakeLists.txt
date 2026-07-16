cmake_minimum_required(VERSION 3.21)
# C 語言:僅供 vendored tinyfiledialogs.c(原生「開啟檔案」對話框,dcc_gui_common)使用。
project(dcc_calibration LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 警告只套用在本專案 targets(第三方原始碼不受影響)。
set(DCC_WARNINGS -Wall -Wextra -Wconversion)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src/dcc_core)
add_subdirectory(src/dcc_io)
add_subdirectory(src/dcc_sim)
add_subdirectory(src/dcc_app)
add_subdirectory(src/dcc_cli)

option(DCC_BUILD_GUI "Build the ImGui desktop workbench (dcc_gui)" ON)
option(DCC_BUILD_SEQVIEW_GUI
      "Build the standalone disp_seq.json viewer GUI (dcc_seqview, zero dependency on dcc_core/io/app)"
      ON)

# dcc_gui_common(imgui_bundle 彙整 + 共用主題/字型):兩支 GUI 任一需要即建置。
if(DCC_BUILD_GUI OR DCC_BUILD_SEQVIEW_GUI)
  add_subdirectory(src/dcc_gui_common)
endif()

# dcc_seqview_core 一律建置(輕量,供 tests 連結);GUI 執行檔視 DCC_BUILD_SEQVIEW_GUI。
add_subdirectory(src/dcc_seqview)

if(DCC_BUILD_GUI)
  add_subdirectory(src/dcc_gui)
endif()

option(DCC_BUILD_TESTS "Build unit tests (SPEC-005 UT-01..10)" ON)
if(DCC_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()

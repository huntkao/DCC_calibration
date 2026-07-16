# 共用 GUI 底座:第三方彙整(imgui_bundle、tinyfiledialogs)+ 主題/字型/檔案對話框工具
# (dcc_core/io/app/sim 零依賴,供 dcc_gui 與 dcc_seqview 共用以保持視覺/字型一致
# ——避免各自複製漂移,但不構成業務邏輯耦合)。
include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9b-docking
  GIT_SHALLOW    TRUE)
FetchContent_Declare(implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        v0.17
  GIT_SHALLOW    TRUE)
# 原生「開啟檔案」對話框(zlib license,單一 .c/.h,無執行階段第三方依賴庫:
# macOS 走 osascript、Windows 走 comdlg32、Linux 走 zenity/kdialog 等現有系統工具)。
FetchContent_Declare(tinyfiledialogs
  GIT_REPOSITORY https://github.com/native-toolkit/tinyfiledialogs.git
  GIT_TAG        2.9.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw imgui implot tinyfiledialogs)

find_package(OpenGL REQUIRED)

# 第三方彙整為獨立 lib(不套本專案警告旗標)。
add_library(imgui_bundle STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(imgui_bundle PUBLIC
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR})
target_link_libraries(imgui_bundle PUBLIC glfw OpenGL::GL)
target_compile_definitions(imgui_bundle PUBLIC GL_SILENCE_DEPRECATION)

# tinyfiledialogs 一樣是第三方原始碼,不套本專案警告旗標(-Wconversion 等會噴)。
add_library(tinyfiledialogs STATIC
  ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c
)
target_include_directories(tinyfiledialogs PUBLIC ${tinyfiledialogs_SOURCE_DIR})

# 主題/字型(header-only)+ 檔案對話框包裝(僅 imgui/implot/tinyfiledialogs API,
# 無本案演算法依賴)。
add_library(dcc_gui_common STATIC
  src/file_dialog.cpp
)
target_include_directories(dcc_gui_common PUBLIC include)
target_link_libraries(dcc_gui_common PUBLIC imgui_bundle tinyfiledialogs)
target_compile_options(dcc_gui_common PRIVATE ${DCC_WARNINGS})

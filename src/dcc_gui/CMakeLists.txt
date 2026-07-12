# 桌面工作台(Dear ImGui docking + ImPlot + GLFW/OpenGL3)。
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
FetchContent_MakeAvailable(glfw imgui implot)

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

add_executable(dcc_gui
  main.cpp
  gui_state.cpp
  panels.cpp
)
target_link_libraries(dcc_gui PRIVATE dcc_app dcc_sim imgui_bundle
                                      nlohmann_json::nlohmann_json)
target_compile_options(dcc_gui PRIVATE ${DCC_WARNINGS})

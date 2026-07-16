# 桌面工作台(Dear ImGui docking + ImPlot + GLFW/OpenGL3;共用底座見 dcc_gui_common)。
add_executable(dcc_gui
  main.cpp
  gui_state.cpp
  panels.cpp
)
target_link_libraries(dcc_gui PRIVATE dcc_app dcc_sim dcc_gui_common
                                      nlohmann_json::nlohmann_json)
target_compile_options(dcc_gui PRIVATE ${DCC_WARNINGS})

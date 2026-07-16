# dcc_seqview:獨立 disp_seq.json 檢視器(SPEC-004 §3a 之外部團隊自檢工具)。
# dcc_seqview_core 刻意零依賴 dcc_core/dcc_io/dcc_app/dcc_sim(見 seq_loader.hpp 註解),
# 僅需 nlohmann_json——外部團隊可只取本目錄 + 已 vendor 之依賴即可建置,無需校正引擎。
add_library(dcc_seqview_core STATIC
  src/seq_loader.cpp
  src/viewer_state.cpp
)
target_include_directories(dcc_seqview_core PUBLIC include)
target_link_libraries(dcc_seqview_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(dcc_seqview_core PRIVATE ${DCC_WARNINGS})

# DCC_BUILD_SEQVIEW_GUI 於頂層 CMakeLists.txt 宣告(與 DCC_BUILD_GUI 同層級)。
if(DCC_BUILD_SEQVIEW_GUI)
  add_executable(dcc_seqview
    main.cpp
    panels.cpp
  )
  target_link_libraries(dcc_seqview PRIVATE dcc_seqview_core dcc_gui_common
                                            nlohmann_json::nlohmann_json)
  target_compile_options(dcc_seqview PRIVATE ${DCC_WARNINGS})
endif()

add_library(dcc_io STATIC
  src/disp_seq_reader.cpp
  src/config.cpp
  src/synth.cpp
)
target_include_directories(dcc_io PUBLIC include)
target_link_libraries(dcc_io PUBLIC dcc_core PRIVATE nlohmann_json::nlohmann_json)

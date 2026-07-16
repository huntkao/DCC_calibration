add_library(dcc_io STATIC
  src/disp_seq_reader.cpp
  src/config.cpp
  src/raw_reader.cpp
  src/logging.cpp
  src/eeprom_equiv.cpp
)
target_include_directories(dcc_io PUBLIC include)
target_link_libraries(dcc_io PUBLIC dcc_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(dcc_io PRIVATE ${DCC_WARNINGS})

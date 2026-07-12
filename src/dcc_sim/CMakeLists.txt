add_library(dcc_sim STATIC
  src/synth.cpp
)
target_include_directories(dcc_sim PUBLIC include)
target_link_libraries(dcc_sim PUBLIC dcc_core PRIVATE nlohmann_json::nlohmann_json)

add_library(dcc_app STATIC
  src/pipeline.cpp
  src/session.cpp
)
target_include_directories(dcc_app PUBLIC include)
target_link_libraries(dcc_app PUBLIC dcc_io PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(dcc_app PRIVATE ${DCC_WARNINGS})

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(dcc_tests
  ut01_sweep_test.cpp
  ut02_reader_test.cpp
  ut03_unit_conversion_test.cpp
  ut04_invalid_samples_test.cpp
  ut05_regression_test.cpp
  ut06_focus_test.cpp
  ut07_validate_test.cpp
  ut08_eeprom_test.cpp
  ut09_units_test.cpp
  ut10_aggregate_test.cpp
  it_pipeline_test.cpp
  raw_test.cpp
)
target_link_libraries(dcc_tests PRIVATE dcc_app dcc_sim nlohmann_json::nlohmann_json
                                        Catch2::Catch2WithMain)
target_include_directories(dcc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(dcc_tests)
target_compile_options(dcc_tests PRIVATE ${DCC_WARNINGS})

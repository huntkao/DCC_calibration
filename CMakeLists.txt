cmake_minimum_required(VERSION 3.21)
project(dcc_calibration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wconversion)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src/dcc_core)
add_subdirectory(src/dcc_io)
add_subdirectory(src/dcc_app)
add_subdirectory(src/dcc_cli)

option(DCC_BUILD_TESTS "Build unit tests (SPEC-005 UT-01..10)" ON)
if(DCC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

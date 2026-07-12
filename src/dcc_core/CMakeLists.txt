add_library(dcc_core STATIC
  src/error.cpp
  src/sweep.cpp
)
target_include_directories(dcc_core PUBLIC include)

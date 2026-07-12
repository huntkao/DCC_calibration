add_library(dcc_core STATIC
  src/error.cpp
  src/sweep.cpp
  src/eeprom_codec.cpp
  src/regression.cpp
  src/focus.cpp
  src/validate.cpp
  src/aggregate.cpp
)
target_include_directories(dcc_core PUBLIC include)

#include "dcc_core/error.hpp"

namespace dcc {

const char* to_string(ErrorCode code) {
  switch (code) {
    case ErrorCode::E_A01: return "E-A01";
    case ErrorCode::E_C01: return "E-C01";
    case ErrorCode::E_D01: return "E-D01";
    case ErrorCode::E_D02: return "E-D02";
    case ErrorCode::E_D03: return "E-D03";
    case ErrorCode::E_E01: return "E-E01";
    case ErrorCode::E_F01: return "E-F01";
    case ErrorCode::E_F02: return "E-F02";
    case ErrorCode::E_G01: return "E-G01";
  }
  return "E-???";
}

}  // namespace dcc

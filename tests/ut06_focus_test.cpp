// UT-06(SPEC-005 §3):focus.peak FR-13。
// 準則:無雜訊序列峰值 420±1;合焦 > NEAR−step(示例 640)→ E-F01。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/error.hpp"
#include "dcc_core/focus.hpp"
#include "dcc_core/sweep.hpp"

using dcc::DccError;
using dcc::ErrorCode;

namespace {
std::vector<int> default_dacs() { return dcc::sweep::plan({0, 1023, 220, 620}, {0.1, 0.1, 10}); }

// 以 center 為峰之單峰曲線(高斯;SPEC-005 §2 合成 focus 模型)。
std::vector<double> gaussian_fv(const std::vector<int>& dacs, double center) {
  std::vector<double> fv;
  for (int dac : dacs) {
    const double t = (static_cast<double>(dac) - center) / 240.0;
    fv.push_back(1000.0 * std::exp(-t * t));
  }
  return fv;
}
}  // namespace

TEST_CASE("UT-06: 合焦 420 之無雜訊序列 → 峰值 420±1", "[ut06][focus]") {
  const auto dacs = default_dacs();
  const double p = dcc::focus::peak(dacs, gaussian_fv(dacs, 420.0));
  REQUIRE(std::fabs(p - 420.0) < 1.0);
}

TEST_CASE("UT-06: 合焦 640(> NEAR−step ≈ 606.7)→ E-F01", "[ut06][focus][error]") {
  const auto dacs = default_dacs();
  try {
    dcc::focus::peak(dacs, gaussian_fv(dacs, 640.0));
    FAIL("應拋出 DccError(E-F01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_F01);
  }
}

TEST_CASE("UT-06: 合焦 200(< FAR+step ≈ 233.3)→ E-F01", "[ut06][focus][error]") {
  const auto dacs = default_dacs();
  try {
    dcc::focus::peak(dacs, gaussian_fv(dacs, 200.0));
    FAIL("應拋出 DccError(E-F01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_F01);
  }
}

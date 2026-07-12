// UT-05(SPEC-005 §3):regression FR-12。
// 準則:無雜訊序列還原 k=12.46 / b=420 至 1e-6;負斜率 → E-E01。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "dcc_core/error.hpp"
#include "dcc_core/regression.hpp"
#include "dcc_core/sweep.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::regression::fit_region;

namespace {
constexpr double kTrueDcc = 12.46;   // 假設模組中央區真值
constexpr double kTrueFocus = 420.0; // 合成合焦位置(SPEC-005 §2)
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::vector<int> default_dacs() { return dcc::sweep::plan({0, 1023, 220, 620}, {0.1, 0.1, 10}); }

std::vector<double> truth_disp(const std::vector<int>& dacs, double sign = 1.0) {
  std::vector<double> d;
  for (int dac : dacs) d.push_back(sign * (static_cast<double>(dac) - kTrueFocus) / kTrueDcc);
  return d;
}
}  // namespace

TEST_CASE("UT-05: 無雜訊序列還原 k=12.46 / b=420 至 1e-6,r² ≈ 1", "[ut05][regression]") {
  const auto dacs = default_dacs();
  const auto r = fit_region(dacs, truth_disp(dacs));
  REQUIRE(std::fabs(r.dcc - kTrueDcc) < 1e-6);
  REQUIRE(std::fabs(r.intercept - kTrueFocus) < 1e-6);
  REQUIRE(r.r2 > 0.999999);
  REQUIRE(r.n_valid == 10);
}

TEST_CASE("UT-05: 負斜率(模擬 L/R 對調)→ E-E01", "[ut05][regression][error]") {
  const auto dacs = default_dacs();
  try {
    fit_region(dacs, truth_disp(dacs, -1.0));
    FAIL("應拋出 DccError(E-E01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_E01);
  }
}

TEST_CASE("UT-05: NaN 樣本剔除;有效 8 點可擬合、7 點 → E-D03", "[ut05][regression]") {
  const auto dacs = default_dacs();
  auto disp = truth_disp(dacs);
  disp[2] = kNaN;
  disp[7] = kNaN;  // 有效 8 點
  const auto r = fit_region(dacs, disp);
  REQUIRE(r.n_valid == 8);
  REQUIRE(std::fabs(r.dcc - kTrueDcc) < 1e-6);

  disp[5] = kNaN;  // 有效 7 點
  try {
    fit_region(dacs, disp);
    FAIL("應拋出 DccError(E-D03)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_D03);
  }
}

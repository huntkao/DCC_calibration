// fitter 擴充(設計:docs/superpowers/specs/2026-07-17-fitter-wls-eiv-design.md)。
// 座標約定:x = disp [raw_pixel]、y = DAC;DAC 精確、disp 含噪(EIV 前提)。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "dcc_core/error.hpp"
#include "dcc_core/regression.hpp"
#include "dcc_core/sweep.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::regression::Fitter;
using dcc::regression::FitOptions;
using dcc::regression::fit_region;

namespace {
constexpr double kTrueDcc = 12.46;
constexpr double kTrueFocus = 420.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::vector<int> default_dacs() { return dcc::sweep::plan({0, 1023, 220, 620}, {0.1, 0.1, 10}); }

std::vector<double> truth_disp(const std::vector<int>& dacs, double sign = 1.0) {
  std::vector<double> d;
  for (int dac : dacs) d.push_back(sign * (static_cast<double>(dac) - kTrueFocus) / kTrueDcc);
  return d;
}
}  // namespace

TEST_CASE("fitter: ols_inverse 無噪還原 k=12.46 / b=420 至 1e-6;與前向等值", "[fitter]") {
  const auto dacs = default_dacs();
  FitOptions fo;
  fo.method = Fitter::ols_inverse;
  const auto inv = fit_region(dacs, truth_disp(dacs), fo);
  REQUIRE(std::fabs(inv.dcc - kTrueDcc) < 1e-6);
  REQUIRE(std::fabs(inv.intercept - kTrueFocus) < 1e-6);
  REQUIRE(inv.r2 > 0.999999);
  REQUIRE(inv.n_valid == 10);
  // 無噪資料上前向/反向應等值(非位元級,1e-9 相對)
  const auto fwd = fit_region(dacs, truth_disp(dacs));
  REQUIRE(std::fabs(inv.dcc - fwd.dcc) / fwd.dcc < 1e-9);
}

TEST_CASE("fitter: ols_inverse 錯誤路徑——負斜率 E-E01、樣本不足 E-D03", "[fitter][error]") {
  const auto dacs = default_dacs();
  FitOptions fo;
  fo.method = Fitter::ols_inverse;
  try {
    fit_region(dacs, truth_disp(dacs, -1.0), fo);
    FAIL("應拋出 DccError(E-E01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_E01);
  }
  auto disp = truth_disp(dacs);
  disp[0] = disp[3] = disp[6] = kNaN;  // 有效 7 點 < 預設門檻 8
  try {
    fit_region(dacs, disp, fo);
    FAIL("應拋出 DccError(E-D03)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_D03);
  }
  REQUIRE(std::string(dcc::regression::to_string(Fitter::ols_inverse)) == "ols_inverse");
}

// UT-07(SPEC-005 §3):validate FR-14。
// 準則:err 邊界值(0.199/0.200/0.201)判定正確(err >= tolerance → FAIL)。
#include <catch2/catch_test_macros.hpp>

#include "dcc_core/validate.hpp"

using dcc::validate::judge;
using dcc::validate::region_pass;

namespace {
constexpr double kSpan = 480.0;  // 預設 config 推導示例(margin 0.1)
constexpr double kTol = 0.20;
constexpr double kIntercept = 420.0;

double peak_with_err(double err) { return kIntercept - err * kSpan; }
}  // namespace

TEST_CASE("UT-07: err 邊界值判定(0.199 PASS / 0.200 FAIL / 0.201 FAIL)", "[ut07][validate]") {
  REQUIRE(region_pass(kIntercept, peak_with_err(0.199), kSpan, kTol));
  REQUIRE_FALSE(region_pass(kIntercept, peak_with_err(0.200), kSpan, kTol));
  REQUIRE_FALSE(region_pass(kIntercept, peak_with_err(0.201), kSpan, kTol));
}

TEST_CASE("UT-07: 全圖判定——單區超容差即模組 FAIL,最差三區排序正確", "[ut07][validate]") {
  // 48 區:全部 err=0.01,僅三區注入 0.25 / 0.15 / 0.05。
  std::vector<double> intercepts(48, kIntercept), peaks(48, peak_with_err(0.01));
  peaks[10] = peak_with_err(0.25);  // FAIL 區
  peaks[20] = peak_with_err(0.15);
  peaks[30] = peak_with_err(0.05);

  const auto j = judge(intercepts, peaks, kSpan, kTol);
  REQUIRE_FALSE(j.pass);
  REQUIRE(j.err.size() == 48);
  REQUIRE(j.worst.size() == 3);
  REQUIRE(j.worst[0].index == 10);
  REQUIRE(j.worst[1].index == 20);
  REQUIRE(j.worst[2].index == 30);

  // 全區在容差內 → PASS。
  peaks[10] = peaks[20] = peaks[30] = peak_with_err(0.01);
  REQUIRE(judge(intercepts, peaks, kSpan, kTol).pass);
}

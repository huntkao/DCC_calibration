// UT-09(SPEC-005 §3):單位契約 SPEC-004 §3。
// 準則:dcc_pd_grid / dcc_raw_px == pitch_x;對漏乘(≈1)/重複乘(≈pitch_x²)正確示警。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/units.hpp"

using namespace dcc::units;

namespace {
constexpr int kPitchX = 16;      // 假設模組
constexpr double kDccRaw = 12.46;
}  // namespace

TEST_CASE("UT-09: 兩單位 DCC 比值 == pitch_x", "[ut09][units]") {
  const double dcc_pd = dcc_raw_px_to_pd_grid(kDccRaw, kPitchX);
  REQUIRE(std::fabs(dcc_pd - 199.36) < 1e-12);
  REQUIRE(dcc_ratio_check(kDccRaw, dcc_pd, kPitchX) == RatioCheck::ok);
}

TEST_CASE("UT-09: disparity 換算互為反函數", "[ut09][units]") {
  const double d = 1.25;  // pd_image_grid
  REQUIRE(std::fabs(pd_grid_to_raw_px(d, kPitchX) - 20.0) < 1e-12);
  REQUIRE(std::fabs(raw_px_to_pd_grid(pd_grid_to_raw_px(d, kPitchX), kPitchX) - d) < 1e-12);
}

TEST_CASE("UT-09: 漏乘(比值≈1)示警", "[ut09][units][error]") {
  REQUIRE(dcc_ratio_check(kDccRaw, kDccRaw, kPitchX) == RatioCheck::missing_pitch_mul);
}

TEST_CASE("UT-09: 重複乘(比值≈pitch_x²)示警", "[ut09][units][error]") {
  REQUIRE(dcc_ratio_check(kDccRaw, kDccRaw * 256.0, kPitchX) == RatioCheck::double_pitch_mul);
}

TEST_CASE("UT-09: 非 1/pitch/pitch² 之異常比值 → inconsistent", "[ut09][units][error]") {
  REQUIRE(dcc_ratio_check(kDccRaw, kDccRaw * 5.0, kPitchX) == RatioCheck::inconsistent);
}

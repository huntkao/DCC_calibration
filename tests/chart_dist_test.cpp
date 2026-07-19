// chart_dist:VCM DAC↔物距薄透鏡近似模型(設計 2026-07-18)。
// DAC = a + b/dist_cm;兩點標定。往返一致、退化輸入 E-A01。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/chart_dist.hpp"
#include "dcc_core/error.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::chart_dist::calibrate_two_point;
using dcc::chart_dist::dac_to_dist;
using dcc::chart_dist::dist_to_dac;

TEST_CASE("chart_dist: 兩點標定 + 往返一致至 1e-9", "[chart_dist]") {
  // 示範標定:INF 220 DAC ≈ 200cm、MACRO 620 DAC ≈ 10cm
  const auto m = calibrate_two_point(220.0, 200.0, 620.0, 10.0);
  // 標定點必須精確還原
  REQUIRE(std::fabs(dist_to_dac(m, 200.0) - 220.0) < 1e-9);
  REQUIRE(std::fabs(dist_to_dac(m, 10.0) - 620.0) < 1e-9);
  // 往返:dac → dist → dac
  for (double dac : {250.0, 420.0, 550.0}) {
    const double d = dac_to_dist(m, dac);
    REQUIRE(d > 0.0);
    REQUIRE(std::fabs(dist_to_dac(m, d) - dac) < 1e-9);
  }
  // 往返:dist → dac → dist
  for (double dist : {15.0, 25.0, 80.0}) {
    const double dac = dist_to_dac(m, dist);
    REQUIRE(std::fabs(dac_to_dist(m, dac) - dist) < 1e-9);
  }
  // 物理性質:DAC 與 1/dist 線性 → 中點 DAC 對應之 1/dist 為兩端 1/dist 平均
  const double dmid = dac_to_dist(m, 420.0);
  REQUIRE(std::fabs(1.0 / dmid - 0.5 * (1.0 / 200.0 + 1.0 / 10.0)) < 1e-9);
}

TEST_CASE("chart_dist: 退化輸入 → E-A01", "[chart_dist][error]") {
  // 兩點 DAC 相同
  REQUIRE_THROWS_AS(calibrate_two_point(300.0, 50.0, 300.0, 20.0), DccError);
  // 兩點物距相同(1/dist 無變異)
  REQUIRE_THROWS_AS(calibrate_two_point(220.0, 50.0, 620.0, 50.0), DccError);
  // 物距 ≤ 0
  REQUIRE_THROWS_AS(calibrate_two_point(220.0, 0.0, 620.0, 10.0), DccError);
  const auto m = calibrate_two_point(220.0, 200.0, 620.0, 10.0);
  REQUIRE_THROWS_AS(dist_to_dac(m, 0.0), DccError);
  REQUIRE_THROWS_AS(dist_to_dac(m, -5.0), DccError);
  // dac 落在 a(物距→∞)→ dac_to_dist 非正,E-A01
  REQUIRE_THROWS_AS(dac_to_dist(m, m.a), DccError);
  try {
    calibrate_two_point(300.0, 50.0, 300.0, 20.0);
    FAIL("應拋出 E-A01");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_A01);
  }
}

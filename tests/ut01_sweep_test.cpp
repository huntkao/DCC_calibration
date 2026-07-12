// UT-01(SPEC-005 §3):sweep 規劃 FR-03/FR-04。
// 準則:端點 == 公式推導值(margin 0.1→180/660;0→220/620;0.05→200/640)、
//       10 點嚴格遞增、端點精確、超界 → E-C01。
#include <catch2/catch_test_macros.hpp>

#include "dcc_core/error.hpp"
#include "dcc_core/sweep.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::sweep::plan;
using dcc::sweep::SweepParams;
using dcc::sweep::VcmParams;

namespace {
// 假設模組(CLAUDE.md):10-bit DAC,AF_CAL_INF=220、AF_CAL_MACRO=620。
VcmParams example_vcm() { return {0, 1023, 220, 620}; }

void check_strictly_increasing(const std::vector<int>& dacs) {
  for (size_t i = 1; i < dacs.size(); ++i) REQUIRE(dacs[i] > dacs[i - 1]);
}
}  // namespace

TEST_CASE("UT-01: 預設 margin 0.1 → 端點 180/660,序列與規格推導示例一致", "[ut01][sweep]") {
  const auto dacs = plan(example_vcm(), {0.1, 0.1, 10});
  REQUIRE(dacs.size() == 10);
  check_strictly_increasing(dacs);
  // SPEC-002 C-1 推導示例
  const std::vector<int> expected = {180, 233, 287, 340, 393, 447, 500, 553, 607, 660};
  REQUIRE(dacs == expected);
}

TEST_CASE("UT-01: margin 0 → 端點 220/620,序列與規格對照數列一致", "[ut01][sweep]") {
  const auto dacs = plan(example_vcm(), {0.0, 0.0, 10});
  const std::vector<int> expected = {220, 264, 309, 353, 398, 442, 487, 531, 576, 620};
  REQUIRE(dacs == expected);
}

TEST_CASE("UT-01: margin 0.05 → 端點 200/640", "[ut01][sweep]") {
  const auto dacs = plan(example_vcm(), {0.05, 0.05, 10});
  REQUIRE(dacs.size() == 10);
  check_strictly_increasing(dacs);
  REQUIRE(dacs.front() == 200);
  REQUIRE(dacs.back() == 640);
}

TEST_CASE("UT-01: 端點超出 DAC 範圍 → E-C01", "[ut01][sweep][error]") {
  // FAR = 30 − 400×0.1 = −10 < dac_min → E-C01
  VcmParams vcm{0, 1023, 30, 430};
  try {
    plan(vcm, {0.1, 0.1, 10});
    FAIL("應拋出 DccError(E-C01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_C01);
  }
}

TEST_CASE("UT-01: 非法參數(INF >= MACRO)→ E-A01", "[ut01][sweep][error]") {
  VcmParams vcm{0, 1023, 620, 220};
  try {
    plan(vcm, {0.1, 0.1, 10});
    FAIL("應拋出 DccError(E-A01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_A01);
  }
}

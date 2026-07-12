// UT-04(SPEC-005 §3):無效樣本 FR-11。
// 準則:null→NaN;單區有效 7 點 → E-D03;8 點通過。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/error.hpp"
#include "dcc_io/disp_seq_reader.hpp"
#include "test_helpers.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::io::load;
using namespace testutil;

namespace {
// 於區 (r=2, c=3) 之前 kill_frames 幀填 null。
SeqSpec spec_with_nulls(int kill_frames) {
  SeqSpec s;
  s.disp = [kill_frames](size_t f, int r, int c) {
    if (r == 2 && c == 3 && f < static_cast<size_t>(kill_frames))
      return std::nan("");  // → null
    return truth_disp_raw_px(default_dacs()[f]);
  };
  return s;
}
}  // namespace

TEST_CASE("UT-04: 2 幀 null → 該區有效 8 點,通過且 NaN 正確落位", "[ut04][reader]") {
  const auto seq = load(make_seq_json(spec_with_nulls(2)).dump(), default_reader_cfg());
  const size_t idx = 2 * 8 + 3;  // (r=2, c=3) row-major
  REQUIRE(std::isnan(seq.disp[0][idx]));
  REQUIRE(std::isnan(seq.disp[1][idx]));
  REQUIRE_FALSE(std::isnan(seq.disp[2][idx]));
  REQUIRE_FALSE(std::isnan(seq.disp[0][0]));  // 其他區不受影響
}

TEST_CASE("UT-04: 3 幀 null → 該區有效 7 點 < 8 → E-D03", "[ut04][reader][error]") {
  try {
    load(make_seq_json(spec_with_nulls(3)).dump(), default_reader_cfg());
    FAIL("應拋出 DccError(E-D03)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_D03);
  }
}

TEST_CASE("UT-04: min_valid_samples 可由 config 調整(7 點在門檻 7 下通過)",
          "[ut04][reader]") {
  auto cfg = default_reader_cfg();
  cfg.min_valid_samples = 7;
  const auto seq = load(make_seq_json(spec_with_nulls(3)).dump(), cfg);
  REQUIRE(seq.disp.size() == 10);
}

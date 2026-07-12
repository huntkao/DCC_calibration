// 輸入/輸出單位:DAC(int)。
#include "dcc_core/sweep.hpp"

#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::sweep {

std::vector<int> plan(const VcmParams& vcm, const SweepParams& sw) {
  if (vcm.af_cal_inf >= vcm.af_cal_macro)
    throw DccError(ErrorCode::E_A01, "AF 校正值非法:INF(" + std::to_string(vcm.af_cal_inf) +
                                         ") >= MACRO(" + std::to_string(vcm.af_cal_macro) + ")");
  if (vcm.dac_min >= vcm.dac_max)
    throw DccError(ErrorCode::E_A01, "DAC 範圍非法:min >= max");
  if (sw.num_positions < 2)
    throw DccError(ErrorCode::E_A01, "num_positions 須 >= 2");
  if (sw.far_margin < 0.0 || sw.far_margin > 0.2 || sw.near_margin < 0.0 || sw.near_margin > 0.2)
    throw DccError(ErrorCode::E_A01, "margin 須在 0..0.2");

  // FAR/NEAR 由公式推導(SPEC-002 C-1),禁止寫死。
  const double span_af = static_cast<double>(vcm.af_cal_macro - vcm.af_cal_inf);
  const double far = static_cast<double>(vcm.af_cal_inf) - span_af * sw.far_margin;
  const double near = static_cast<double>(vcm.af_cal_macro) + span_af * sw.near_margin;
  const double step = (near - far) / static_cast<double>(sw.num_positions - 1);

  std::vector<int> dacs(static_cast<size_t>(sw.num_positions));
  for (int i = 0; i < sw.num_positions; ++i)
    dacs[static_cast<size_t>(i)] =
        static_cast<int>(std::lround(far + step * static_cast<double>(i)));

  // 端點超出實體 DAC 範圍 → E-C01(FR-03)。
  if (dacs.front() < vcm.dac_min || dacs.back() > vcm.dac_max)
    throw DccError(ErrorCode::E_C01,
                   "sweep 端點超出 DAC 範圍:FAR=" + std::to_string(dacs.front()) +
                       ", NEAR=" + std::to_string(dacs.back()) + ",允許 [" +
                       std::to_string(vcm.dac_min) + ", " + std::to_string(vcm.dac_max) + "]");

  // 嚴格遞增檢核(鐵律 3/5:FAR→NEAR 單向;算不攏就停下來)。
  for (size_t i = 1; i < dacs.size(); ++i)
    if (dacs[i] <= dacs[i - 1])
      throw DccError(ErrorCode::E_C01, "sweep 序列非嚴格遞增(step 過小?)");

  return dacs;
}

}  // namespace dcc::sweep

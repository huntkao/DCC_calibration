// 輸入/輸出單位:DAC(int)。
// 掃描規劃(SPEC-002 C-1 / FR-03 / FR-04):端點由公式推導,禁止寫死;
// 序列固定 FAR→NEAR 嚴格遞增(鐵律 3,不提供反轉參數)。
#pragma once

#include <vector>

namespace dcc::sweep {

struct VcmParams {
  int dac_min = 0;
  int dac_max = 1023;
  int af_cal_inf = 0;    // AF 校正:無窮遠端 DAC
  int af_cal_macro = 0;  // AF 校正:近端 DAC(須 > af_cal_inf)
};

struct SweepParams {
  double far_margin = 0.1;   // 0..0.2(SPEC-004 §2,預設 0.1)
  double near_margin = 0.1;  // 0..0.2
  int num_positions = 10;    // v1 固定 10
};

// 回傳長度 num_positions 之 DAC 序列;嚴格遞增、端點精確(四捨五入後)。
// FAR = inf − span_af×far_margin、NEAR = macro + span_af×near_margin、
// step = (NEAR−FAR)/(n−1)。
// 失敗:參數非法 → DccError(E_A01);端點超出 [dac_min, dac_max] → DccError(E_C01)。
std::vector<int> plan(const VcmParams& vcm, const SweepParams& sw);

}  // namespace dcc::sweep

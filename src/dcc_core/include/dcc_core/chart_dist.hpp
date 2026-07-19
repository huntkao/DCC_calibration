// 輸入單位:dac = DAC(無量綱刻度)、dist_cm = 物距公分(正值)。
// 輸出單位:dac_to_dist → 物距 cm;dist_to_dac → DAC。
// VCM DAC↔物距薄透鏡近似(設計 2026-07-18):DAC = a + b/dist_cm(DAC 與 1/物距 線性)。
// 兩點標定求 a、b(給鏡頭專屬 INF/MACRO 兩個 DAC↔物距對照點)。
#pragma once

namespace dcc::chart_dist {

struct VcmDistModel {
  double a = 0.0;  // 截距(dac 當物距→∞)
  double b = 0.0;  // 斜率(DAC per (1/cm))
};

// 兩點標定。失敗:dac1==dac2、1/dist1==1/dist2、任一 dist ≤ 0 → DccError(E_A01)。
VcmDistModel calibrate_two_point(double dac1, double dist1_cm, double dac2, double dist2_cm);

// DAC → 物距 cm。失敗:結果非正(dac 落在 a 或錯側)→ DccError(E_A01)。
double dac_to_dist(const VcmDistModel& m, double dac);

// 物距 cm → DAC。失敗:dist_cm ≤ 0 → DccError(E_A01)。
double dist_to_dac(const VcmDistModel& m, double dist_cm);

}  // namespace dcc::chart_dist

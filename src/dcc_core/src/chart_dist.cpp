#include "dcc_core/chart_dist.hpp"

#include "dcc_core/error.hpp"

namespace dcc::chart_dist {

VcmDistModel calibrate_two_point(double dac1, double dist1_cm, double dac2, double dist2_cm) {
  if (dist1_cm <= 0.0 || dist2_cm <= 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:物距須為正");
  if (dac1 == dac2)
    throw DccError(ErrorCode::E_A01, "chart_dist:兩標定點 DAC 相同,無法解斜率");
  const double inv1 = 1.0 / dist1_cm;
  const double inv2 = 1.0 / dist2_cm;
  if (inv1 == inv2)
    throw DccError(ErrorCode::E_A01, "chart_dist:兩標定點物距相同,無法解斜率");
  VcmDistModel m;
  m.b = (dac1 - dac2) / (inv1 - inv2);
  m.a = dac1 - m.b * inv1;
  return m;
}

double dac_to_dist(const VcmDistModel& m, double dac) {
  const double denom = dac - m.a;
  if (denom == 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:dac 對應物距無窮遠");
  const double dist = m.b / denom;
  if (dist <= 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:dac 換算物距非正(超出模型有效域)");
  return dist;
}

double dist_to_dac(const VcmDistModel& m, double dist_cm) {
  if (dist_cm <= 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:物距須為正");
  return m.a + m.b / dist_cm;
}

}  // namespace dcc::chart_dist

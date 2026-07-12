// 單位契約(SPEC-004 §3,全案最高優先):
// disparity 內部一律 raw_pixel;pd_image_grid ↔ raw_pixel 換算僅允許
// 讀取端/打包端呼叫本模組。DCC 單位 DAC/raw_pixel。
#pragma once

#include <cmath>

namespace dcc::units {

// 輸入 pd_image_grid → 輸出 raw_pixel。
inline double pd_grid_to_raw_px(double value_pd_grid, int pitch_x) {
  return value_pd_grid * static_cast<double>(pitch_x);
}

// 輸入 raw_pixel → 輸出 pd_image_grid。
inline double raw_px_to_pd_grid(double value_raw_px, int pitch_x) {
  return value_raw_px / static_cast<double>(pitch_x);
}

// DCC 換算:DAC/raw_pixel → DAC/pd_image_grid(數值放大 pitch_x 倍)。
inline double dcc_raw_px_to_pd_grid(double dcc_raw_px, int pitch_x) {
  return dcc_raw_px * static_cast<double>(pitch_x);
}

// 16 倍檢核(SPEC-004 §3):dcc_pd_grid / dcc_raw_px 應恆等於 pitch_x;
// ≈1 = 漏乘 pitch_x、≈pitch_x² = 重複乘。
enum class RatioCheck { ok, missing_pitch_mul, double_pitch_mul, inconsistent };

inline RatioCheck dcc_ratio_check(double dcc_raw_px, double dcc_pd_grid, int pitch_x,
                                  double rel_tol = 0.02) {
  if (dcc_raw_px <= 0.0) return RatioCheck::inconsistent;
  const double ratio = dcc_pd_grid / dcc_raw_px;
  const double p = static_cast<double>(pitch_x);
  const auto near = [rel_tol](double a, double b) { return std::fabs(a - b) <= rel_tol * b; };
  if (near(ratio, p)) return RatioCheck::ok;
  if (near(ratio, 1.0)) return RatioCheck::missing_pitch_mul;
  if (near(ratio, p * p)) return RatioCheck::double_pitch_mul;
  return RatioCheck::inconsistent;
}

}  // namespace dcc::units

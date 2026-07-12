// 輸入單位:dacs = DAC(int)、disp = raw_pixel(NaN = 無效樣本)。
// 輸出單位:dcc = DAC/raw_pixel(恆正)、intercept = DAC。
// 逐區最小平方擬合 DAC = k·disp + b(FR-12;v1 一般 OLS,fitter 可抽換——
// EIV 修正列開發備忘 SPEC-005 §8 #2)。
#pragma once

#include <vector>

namespace dcc::regression {

struct FitResult {
  double dcc = 0.0;        // 斜率 k [DAC/raw_pixel]
  double intercept = 0.0;  // 截距 b [DAC](物理意義:合焦點)
  double r2 = 0.0;         // 擬合品質(< r2_warn 由上層警告)
  int n_valid = 0;         // 參與擬合之有效樣本數
};

// 單一區域擬合。
// 失敗:有效樣本 < min_valid_samples → DccError(E_D03);
//       斜率 k <= 0 → DccError(E_E01)(提示 LEFT/RIGHT 檢查)。
FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     int min_valid_samples = 8);

}  // namespace dcc::regression

#include "dcc_core/regression.hpp"

#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::regression {

FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     int min_valid_samples) {
  if (dacs.size() != disp.size())
    throw DccError(ErrorCode::E_D01, "dacs 與 disp 長度不一致(閉環驗算失敗)");

  // 累加有效樣本(x = disp [raw_px], y = dac [DAC])。
  double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  int n = 0;
  for (size_t i = 0; i < disp.size(); ++i) {
    if (std::isnan(disp[i])) continue;
    const double x = disp[i];
    const double y = static_cast<double>(dacs[i]);
    sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
    ++n;
  }
  if (n < min_valid_samples)
    throw DccError(ErrorCode::E_D03, "區域有效樣本不足:" + std::to_string(n) + " < " +
                                         std::to_string(min_valid_samples));

  const double dn = static_cast<double>(n);
  const double sxx_c = sxx - sx * sx / dn;  // Σ(x-x̄)²
  const double sxy_c = sxy - sx * sy / dn;  // Σ(x-x̄)(y-ȳ)
  const double syy_c = syy - sy * sy / dn;  // Σ(y-ȳ)²
  if (sxx_c <= 0.0)
    throw DccError(ErrorCode::E_D03, "disparity 無變異,無法擬合");

  FitResult r;
  r.n_valid = n;
  r.dcc = sxy_c / sxx_c;
  r.intercept = (sy - r.dcc * sx) / dn;
  r.r2 = (syy_c > 0.0) ? (sxy_c * sxy_c) / (sxx_c * syy_c) : 0.0;

  if (r.dcc <= 0.0)
    throw DccError(ErrorCode::E_E01,
                   "回歸斜率非正(k=" + std::to_string(r.dcc) + "),請檢查 LEFT/RIGHT 定義");
  return r;
}

}  // namespace dcc::regression

#include "dcc_core/regression.hpp"

#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::regression {

const char* to_string(Fitter f) {
  switch (f) {
    case Fitter::ols_forward: return "ols_forward";
    case Fitter::ols_inverse: return "ols_inverse";
    case Fitter::wls_inverse: return "wls_inverse";
    case Fitter::deming: return "deming";
  }
  return "?";
}

FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     const FitOptions& opts, int min_valid_samples) {
  if (dacs.size() != disp.size())
    throw DccError(ErrorCode::E_D01, "dacs 與 disp 長度不一致(閉環驗算失敗)");
  if (opts.weights && opts.weights->size() != disp.size())
    throw DccError(ErrorCode::E_D01, "weights 與 disp 長度不一致(閉環驗算失敗)");

  // 加權累加(x = disp [raw_px], y = dac [DAC];非 WLS 時 w ≡ 1.0,與 v1 位元級等價)。
  const bool use_w = (opts.method == Fitter::wls_inverse) && opts.weights;
  double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  int n = 0;
  for (size_t i = 0; i < disp.size(); ++i) {
    if (std::isnan(disp[i])) continue;
    ++n;  // n_valid = 非 NaN 計數(權重不影響;w=0 形同剔除但仍計數)
    double w = 1.0;
    if (use_w) {
      w = (*opts.weights)[i];
      if (!(w > 0.0)) continue;  // w ≤ 0 或 NaN → 不入累加(SPEC-004 §3a.1 q=0 語意)
    }
    const double x = disp[i];
    const double y = static_cast<double>(dacs[i]);
    sw += w; sx += w * x; sy += w * y;
    sxx += w * x * x; sxy += w * x * y; syy += w * y * y;
  }
  if (n < min_valid_samples)
    throw DccError(ErrorCode::E_D03, "區域有效樣本不足:" + std::to_string(n) + " < " +
                                         std::to_string(min_valid_samples));
  if (use_w && sw <= 0.0)
    throw DccError(ErrorCode::E_D03, "WLS 權重總和非正(quality 全零?)");

  const double sxx_c = sxx - sx * sx / sw;  // Σw(x-x̄w)²
  const double sxy_c = sxy - sx * sy / sw;  // Σw(x-x̄w)(y-ȳw)
  const double syy_c = syy - sy * sy / sw;  // Σw(y-ȳw)²

  FitResult r;
  r.n_valid = n;
  switch (opts.method) {
    case Fitter::ols_forward:
      if (sxx_c <= 0.0) throw DccError(ErrorCode::E_D03, "disparity 無變異,無法擬合");
      r.dcc = sxy_c / sxx_c;
      break;
    case Fitter::ols_inverse:
    case Fitter::wls_inverse:
      if (syy_c <= 0.0) throw DccError(ErrorCode::E_D03, "DAC 無變異,無法擬合");
      if (sxy_c == 0.0)
        throw DccError(ErrorCode::E_E01, "共變異為零,斜率未定義,請檢查 LEFT/RIGHT 定義");
      r.dcc = syy_c / sxy_c;
      break;
    case Fitter::deming: {
      if (sxx_c <= 0.0) throw DccError(ErrorCode::E_D03, "disparity 無變異,無法擬合");
      if (syy_c <= 0.0) throw DccError(ErrorCode::E_D03, "DAC 無變異,無法擬合");
      if (sxy_c == 0.0)
        throw DccError(ErrorCode::E_E01, "共變異為零,斜率未定義,請檢查 LEFT/RIGHT 定義");
      const double d = opts.deming_delta;
      if (d == 0.0) {
        r.dcc = syy_c / sxy_c;  // δ=0 = DAC 無噪之極限,恆等反向(閉式解代數極限)
      } else {
        const double t = syy_c - d * sxx_c;
        r.dcc = (t + std::sqrt(t * t + 4.0 * d * sxy_c * sxy_c)) / (2.0 * sxy_c);
      }
      break;
    }
  }
  r.intercept = (sy - r.dcc * sx) / sw;
  r.r2 = (sxx_c > 0.0 && syy_c > 0.0) ? (sxy_c * sxy_c) / (sxx_c * syy_c) : 0.0;

  if (r.dcc <= 0.0)
    throw DccError(ErrorCode::E_E01,
                   "回歸斜率非正(k=" + std::to_string(r.dcc) + "),請檢查 LEFT/RIGHT 定義");
  return r;
}

FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     int min_valid_samples) {
  return fit_region(dacs, disp, FitOptions{}, min_valid_samples);
}

}  // namespace dcc::regression

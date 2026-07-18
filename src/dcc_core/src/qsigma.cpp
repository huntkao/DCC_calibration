#include "dcc_core/qsigma.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::qsigma {

Result calibrate(const std::vector<double>& q, const std::vector<double>& abs_resid,
                 int n_bins) {
  if (q.size() != abs_resid.size())
    throw DccError(ErrorCode::E_D01, "q 與 abs_resid 長度不一致(閉環驗算失敗)");
  if (n_bins < 3) throw DccError(ErrorCode::E_D03, "qsigma 箱數須 ≥ 3");

  // 收有效樣本並按 q 排序(等頻分箱基準)。
  std::vector<std::pair<double, double>> s;  // (q, |e|)
  for (size_t i = 0; i < q.size(); ++i)
    if (std::isfinite(q[i]) && q[i] > 0.0 && std::isfinite(abs_resid[i]) && abs_resid[i] >= 0.0)
      s.emplace_back(q[i], abs_resid[i]);
  // 僅依 q 排序(stable_sort 保留同 q 樣本之原順序)。
  // 注意:std::sort 對 pair 預設為字典序,q 重複時會以 |e| 為次鍵排序,
  // 導致等頻分箱在重複 q(如離散 DAC 掃描僅 5 種相異 q)下把箱切在
  // 已依殘差排序過的子區間內,產生系統性偏誤(UT-Q2 曾因此得到 p 為負值)。
  std::stable_sort(s.begin(), s.end(),
                    [](const std::pair<double, double>& a, const std::pair<double, double>& b) {
                      return a.first < b.first;
                    });

  // 等頻分箱:箱 m 取樣本 [m·N/n_bins, (m+1)·N/n_bins);每箱 (log q̄, log RMS)。
  std::vector<double> lx, ly;
  const size_t N = s.size();
  for (int m = 0; m < n_bins; ++m) {
    const size_t lo = N * static_cast<size_t>(m) / static_cast<size_t>(n_bins);
    const size_t hi = N * static_cast<size_t>(m + 1) / static_cast<size_t>(n_bins);
    if (hi - lo < 2) continue;
    double qm = 0.0, rss = 0.0;
    for (size_t i = lo; i < hi; ++i) {
      qm += s[i].first;
      rss += s[i].second * s[i].second;
    }
    const double cnt = static_cast<double>(hi - lo);
    const double sig = std::sqrt(rss / cnt);
    if (sig <= 0.0) continue;  // 全零殘差箱無資訊
    lx.push_back(std::log(qm / cnt));
    ly.push_back(std::log(sig));
  }
  if (lx.size() < 3)
    throw DccError(ErrorCode::E_D03,
                   "qsigma 可用箱不足:" + std::to_string(lx.size()) + " < 3");

  // log-log OLS:log σ̂ = c − p·log q̄。
  const double n = static_cast<double>(lx.size());
  double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  for (size_t i = 0; i < lx.size(); ++i) {
    sx += lx[i]; sy += ly[i];
    sxx += lx[i] * lx[i]; sxy += lx[i] * ly[i]; syy += ly[i] * ly[i];
  }
  const double sxx_c = sxx - sx * sx / n;
  const double sxy_c = sxy - sx * sy / n;
  const double syy_c = syy - sy * sy / n;
  if (sxx_c <= 0.0) throw DccError(ErrorCode::E_D03, "qsigma:log q 無變異,無法擬合");

  Result r;
  r.bins_used = static_cast<int>(lx.size());
  const double slope = sxy_c / sxx_c;
  r.p = -slope;
  r.sigma0 = std::exp((sy - slope * sx) / n);
  r.r2 = (syy_c > 0.0) ? (sxy_c * sxy_c) / (sxx_c * syy_c) : 0.0;
  return r;
}

}  // namespace dcc::qsigma

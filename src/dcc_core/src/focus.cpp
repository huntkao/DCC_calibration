#include "dcc_core/focus.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::focus {

namespace {

// 高斯消去(部分主元)解小型線性系統 A·x = b;A 為 (m×m) row-major。
std::vector<double> solve_linear(std::vector<double> a, std::vector<double> b, size_t m) {
  for (size_t col = 0; col < m; ++col) {
    size_t piv = col;
    for (size_t r = col + 1; r < m; ++r)
      if (std::fabs(a[r * m + col]) > std::fabs(a[piv * m + col])) piv = r;
    if (std::fabs(a[piv * m + col]) < 1e-14)
      throw DccError(ErrorCode::E_D03, "focus 擬合矩陣奇異(樣本退化)");
    if (piv != col) {
      for (size_t c = 0; c < m; ++c) std::swap(a[col * m + c], a[piv * m + c]);
      std::swap(b[col], b[piv]);
    }
    for (size_t r = col + 1; r < m; ++r) {
      const double f = a[r * m + col] / a[col * m + col];
      for (size_t c = col; c < m; ++c) a[r * m + c] -= f * a[col * m + c];
      b[r] -= f * b[col];
    }
  }
  std::vector<double> x(m);
  for (size_t ri = m; ri-- > 0;) {
    double acc = b[ri];
    for (size_t c = ri + 1; c < m; ++c) acc -= a[ri * m + c] * x[c];
    x[ri] = acc / a[ri * m + ri];
  }
  return x;
}

}  // namespace

double peak(const std::vector<int>& dacs, const std::vector<double>& fv, int poly_order,
            int peak_margin_steps) {
  if (dacs.size() != fv.size())
    throw DccError(ErrorCode::E_D01, "dacs 與 fv 長度不一致(閉環驗算失敗)");

  // 收集有效樣本;x 正規化至 [-1,1] 以改善條件數。
  const double dac_min = static_cast<double>(dacs.front());
  const double dac_max = static_cast<double>(dacs.back());
  const double center = 0.5 * (dac_min + dac_max);
  const double half = 0.5 * (dac_max - dac_min);
  if (half <= 0.0) throw DccError(ErrorCode::E_D01, "sweep 範圍退化");

  std::vector<double> xs, ys;
  for (size_t i = 0; i < fv.size(); ++i) {
    if (std::isnan(fv[i])) continue;
    xs.push_back((static_cast<double>(dacs[i]) - center) / half);
    ys.push_back(fv[i]);
  }
  const size_t m = static_cast<size_t>(poly_order) + 1;
  if (xs.size() <= static_cast<size_t>(poly_order))
    throw DccError(ErrorCode::E_D03, "focus 有效樣本不足以擬合 " + std::to_string(poly_order) +
                                         " 階多項式:" + std::to_string(xs.size()));

  // 正規方程 (VᵀV)·c = Vᵀy,V[i][j] = x_i^j。
  std::vector<double> ata(m * m, 0.0), aty(m, 0.0);
  for (size_t i = 0; i < xs.size(); ++i) {
    double pj = 1.0;
    std::vector<double> pow_x(2 * m - 1);
    for (size_t j = 0; j < 2 * m - 1; ++j) { pow_x[j] = pj; pj *= xs[i]; }
    for (size_t r = 0; r < m; ++r) {
      for (size_t c = 0; c < m; ++c) ata[r * m + c] += pow_x[r + c];
      aty[r] += pow_x[r] * ys[i];
    }
  }
  const std::vector<double> coef = solve_linear(std::move(ata), std::move(aty), m);

  // 密集取樣求峰值(範圍 [dac_min, dac_max],解析度 ~0.1 DAC)。
  const int samples = 4801;
  double best_x = -1.0, best_y = -1e300;
  for (int s = 0; s < samples; ++s) {
    const double t = -1.0 + 2.0 * static_cast<double>(s) / static_cast<double>(samples - 1);
    double y = 0.0, p = 1.0;
    for (size_t j = 0; j < m; ++j) { y += coef[j] * p; p *= t; }
    if (y > best_y) { best_y = y; best_x = t; }
  }
  const double peak_dac = center + best_x * half;

  // 出界檢查(FR-13):峰值須嚴格在端點 ± margin_steps×step 之內。
  const double step = (dac_max - dac_min) / static_cast<double>(dacs.size() - 1);
  const double lo = dac_min + static_cast<double>(peak_margin_steps) * step;
  const double hi = dac_max - static_cast<double>(peak_margin_steps) * step;
  if (!(peak_dac > lo && peak_dac < hi))
    throw DccError(ErrorCode::E_F01, "focus 峰值出界:" + std::to_string(peak_dac) +
                                         ",有效範圍 (" + std::to_string(lo) + ", " +
                                         std::to_string(hi) + ");檢查 chart 距離");
  return peak_dac;
}

}  // namespace dcc::focus

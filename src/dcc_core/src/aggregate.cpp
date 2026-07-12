#include "dcc_core/aggregate.hpp"

#include <algorithm>
#include <cmath>

#include "dcc_core/error.hpp"

namespace dcc::aggregate {

namespace {

// cell 中心(index + 0.5)對 out_dim 等分歸區;邊界歸左/上。
int region_of(int cell_index, int in_dim, int out_dim) {
  const double t =
      (static_cast<double>(cell_index) + 0.5) * static_cast<double>(out_dim) / static_cast<double>(in_dim);
  int idx = static_cast<int>(std::floor(t));
  if (static_cast<double>(idx) == t && idx > 0) --idx;  // 恰在邊界 → 歸左/上區
  if (idx >= out_dim) idx = out_dim - 1;
  return idx;
}

double median_of(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

std::vector<double> aggregate(const std::vector<double>& cells, GridSize in, GridSize out,
                              Method method, const std::vector<double>* quality,
                              double min_valid_ratio) {
  const size_t in_n = static_cast<size_t>(in.w) * static_cast<size_t>(in.h);
  if (cells.size() != in_n)
    throw DccError(ErrorCode::E_D01, "aggregate:cells 長度與 in 尺寸閉環驗算失敗");
  if (quality && !quality->empty() && quality->size() != in_n)
    throw DccError(ErrorCode::E_D01, "aggregate:quality 長度與 in 尺寸閉環驗算失敗");

  // 直通(D-5 跳過):粒度一致。
  if (in.w == out.w && in.h == out.h) return cells;

  const size_t out_n = static_cast<size_t>(out.w) * static_cast<size_t>(out.h);
  std::vector<std::vector<double>> vals(out_n), wts(out_n);
  std::vector<int> total(out_n, 0);

  for (int r = 0; r < in.h; ++r) {
    const int orow = region_of(r, in.h, out.h);
    for (int c = 0; c < in.w; ++c) {
      const int ocol = region_of(c, in.w, out.w);
      const size_t oi = static_cast<size_t>(orow) * static_cast<size_t>(out.w) +
                        static_cast<size_t>(ocol);
      const size_t ii = static_cast<size_t>(r) * static_cast<size_t>(in.w) +
                        static_cast<size_t>(c);
      ++total[oi];
      if (std::isnan(cells[ii])) continue;
      vals[oi].push_back(cells[ii]);
      double w = 1.0;
      if (quality && !quality->empty()) {
        const double q = (*quality)[ii];
        w = (std::isfinite(q) && q > 0.0) ? q : 0.0;
      }
      wts[oi].push_back(w);
    }
  }

  std::vector<double> result(out_n, std::nan(""));
  for (size_t i = 0; i < out_n; ++i) {
    if (total[i] == 0) continue;
    const double ratio = static_cast<double>(vals[i].size()) / static_cast<double>(total[i]);
    if (ratio < min_valid_ratio) continue;  // 有效比例不足 → NaN(D-5)

    if (method == Method::median) {
      result[i] = median_of(vals[i]);
    } else {
      double sw = 0.0, swv = 0.0;
      for (size_t k = 0; k < vals[i].size(); ++k) { sw += wts[i][k]; swv += wts[i][k] * vals[i][k]; }
      if (sw <= 0.0) {  // 權重全零 → 退回等權
        for (double v : vals[i]) swv += v;
        result[i] = swv / static_cast<double>(vals[i].size());
      } else {
        result[i] = swv / sw;
      }
    }
  }
  return result;
}

}  // namespace dcc::aggregate

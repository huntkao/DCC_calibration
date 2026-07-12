#include "dcc_core/validate.hpp"

#include <algorithm>
#include <cmath>

#include "dcc_core/error.hpp"

namespace dcc::validate {

bool region_pass(double intercept, double focus_peak, double span, double tolerance) {
  const double err = std::fabs(intercept - focus_peak) / span;
  return err < tolerance;
}

Judgement judge(const std::vector<double>& intercepts, const std::vector<double>& peaks,
                double span, double tolerance) {
  if (intercepts.size() != peaks.size())
    throw DccError(ErrorCode::E_D01, "intercepts 與 peaks 長度不一致(閉環驗算失敗)");

  Judgement j;
  j.pass = true;
  j.err.resize(intercepts.size());
  for (size_t i = 0; i < intercepts.size(); ++i) {
    j.err[i] = std::fabs(intercepts[i] - peaks[i]) / span;
    if (!(j.err[i] < tolerance)) j.pass = false;
  }

  // 最差三區(報告用,FR-17)。
  std::vector<WorstRegion> all;
  all.reserve(j.err.size());
  for (size_t i = 0; i < j.err.size(); ++i) all.push_back({i, j.err[i]});
  std::sort(all.begin(), all.end(),
            [](const WorstRegion& a, const WorstRegion& b) { return a.err > b.err; });
  const size_t k = std::min<size_t>(3, all.size());
  j.worst.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(k));
  return j;
}

}  // namespace dcc::validate

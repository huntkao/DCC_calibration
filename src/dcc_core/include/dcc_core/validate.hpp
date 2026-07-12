// 輸入單位:intercept / focus_peak = DAC、span = DAC(NEAR−FAR,以實際 sweep 推導)。
// 交叉驗證判定(FR-14):err = |intercept − focus_peak| / span;
// err >= tolerance → 該區 FAIL;任一區 FAIL → 模組 FAIL。
#pragma once

#include <cstddef>
#include <vector>

namespace dcc::validate {

struct WorstRegion {
  std::size_t index = 0;  // 展平索引(row-major,r*grid_w+c 由上層解讀)
  double err = 0.0;
};

struct Judgement {
  bool pass = false;
  std::vector<double> err;           // 逐區誤差(與輸入同長)
  std::vector<WorstRegion> worst;    // 最差三區(err 由大到小)
};

// 單區判定:err < tolerance → PASS(邊界值 err == tolerance → FAIL,UT-07)。
bool region_pass(double intercept, double focus_peak, double span, double tolerance);

// 全圖判定;intercepts 與 peaks 長度須一致。
Judgement judge(const std::vector<double>& intercepts, const std::vector<double>& peaks,
                double span, double tolerance);

}  // namespace dcc::validate

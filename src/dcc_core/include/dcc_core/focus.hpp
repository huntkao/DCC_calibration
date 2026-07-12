// 輸入單位:dacs = DAC(int)、fv = focus value(量綱不拘,同序列內一致)。
// 輸出單位:DAC(double)。
// 逐區 focus 曲線多項式擬合取峰值(FR-13)。
#pragma once

#include <vector>

namespace dcc::focus {

// 以 poly_order 階最小平方擬合 fv(dac),回傳峰值位置(DAC)。
// 峰值須嚴格落在 (FAR + margin_steps×step, NEAR − margin_steps×step) 內,
// 否則 DccError(E_F01)(chart 距離/場曲提示)。
// NaN 之 fv 樣本會被略過;有效樣本 <= poly_order → DccError(E_D03)。
double peak(const std::vector<int>& dacs, const std::vector<double>& fv, int poly_order = 4,
            int peak_margin_steps = 1);

}  // namespace dcc::focus

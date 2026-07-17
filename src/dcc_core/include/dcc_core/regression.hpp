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

// fitter 選擇(設計文件 2026-07-17;預設 ols_forward = v1 行為)。
// 座標約定:x = disp [raw_pixel]、y = DAC。DAC 為指令值(無噪),disp 為量測(含噪)——
// 前向 OLS 有衰減偏差;反向(擬合 disp = a·DAC + c 後 k = 1/a)無偏。
enum class Fitter {
  ols_forward,  // v1 現況:DAC = k·disp + b 之 OLS
  ols_inverse,  // 反向:k = syy_c/sxy_c(EIV 消偏)
  wls_inverse,  // 反向 + 每幀權重(Task 2)
  deming,       // 廣義 EIV;δ=0 ≡ 反向、δ→∞ ≡ 前向(Task 3)
};

const char* to_string(Fitter f);  // "ols_forward" 等(config/report/GUI 共用字串)

struct FitOptions {
  Fitter method = Fitter::ols_forward;
  const std::vector<double>* weights = nullptr;  // 每幀權重(僅 wls_inverse 用;nullptr = 等權)
  double deming_delta = 0.0;                     // δ = σ_DAC²/σ_disp²(0 = DAC 無噪之極限)
};

// 單一區域擬合。
// 失敗:有效樣本 < min_valid_samples → DccError(E_D03);
//       斜率 k <= 0 → DccError(E_E01)(提示 LEFT/RIGHT 檢查)。
FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     int min_valid_samples = 8);

// options 版;舊兩參數版保留且 ≡ ols_forward(位元級不變)。
FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     const FitOptions& opts, int min_valid_samples = 8);

}  // namespace dcc::regression

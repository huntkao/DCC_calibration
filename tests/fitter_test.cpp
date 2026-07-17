// fitter 擴充(設計:docs/superpowers/specs/2026-07-17-fitter-wls-eiv-design.md)。
// 座標約定:x = disp [raw_pixel]、y = DAC;DAC 精確、disp 含噪(EIV 前提)。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

#include "dcc_core/error.hpp"
#include "dcc_core/regression.hpp"
#include "dcc_core/sweep.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::regression::Fitter;
using dcc::regression::FitOptions;
using dcc::regression::fit_region;

namespace {
constexpr double kTrueDcc = 12.46;
constexpr double kTrueFocus = 420.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::vector<int> default_dacs() { return dcc::sweep::plan({0, 1023, 220, 620}, {0.1, 0.1, 10}); }

std::vector<double> truth_disp(const std::vector<int>& dacs, double sign = 1.0) {
  std::vector<double> d;
  for (int dac : dacs) d.push_back(sign * (static_cast<double>(dac) - kTrueFocus) / kTrueDcc);
  return d;
}

// 樣本平均與變異(母體 n 除;測試內部統計用)
double mean_of(const std::vector<double>& v) {
  double s = 0.0;
  for (double x : v) s += x;
  return s / static_cast<double>(v.size());
}
double var_of(const std::vector<double>& v) {
  const double m = mean_of(v);
  double s = 0.0;
  for (double x : v) s += (x - m) * (x - m);
  return s / static_cast<double>(v.size());
}
}  // namespace

TEST_CASE("fitter: ols_inverse 無噪還原 k=12.46 / b=420 至 1e-6;與前向等值", "[fitter]") {
  const auto dacs = default_dacs();
  FitOptions fo;
  fo.method = Fitter::ols_inverse;
  const auto inv = fit_region(dacs, truth_disp(dacs), fo);
  REQUIRE(std::fabs(inv.dcc - kTrueDcc) < 1e-6);
  REQUIRE(std::fabs(inv.intercept - kTrueFocus) < 1e-6);
  REQUIRE(inv.r2 > 0.999999);
  REQUIRE(inv.n_valid == 10);
  // 無噪資料上前向/反向應等值(非位元級,1e-9 相對)
  const auto fwd = fit_region(dacs, truth_disp(dacs));
  REQUIRE(std::fabs(inv.dcc - fwd.dcc) / fwd.dcc < 1e-9);
}

TEST_CASE("fitter: ols_inverse 錯誤路徑——負斜率 E-E01、樣本不足 E-D03", "[fitter][error]") {
  const auto dacs = default_dacs();
  FitOptions fo;
  fo.method = Fitter::ols_inverse;
  try {
    fit_region(dacs, truth_disp(dacs, -1.0), fo);
    FAIL("應拋出 DccError(E-E01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_E01);
  }
  auto disp = truth_disp(dacs);
  disp[0] = disp[3] = disp[6] = kNaN;  // 有效 7 點 < 預設門檻 8
  try {
    fit_region(dacs, disp, fo);
    FAIL("應拋出 DccError(E-D03)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_D03);
  }
  REQUIRE(std::string(dcc::regression::to_string(Fitter::ols_inverse)) == "ols_inverse");
}

TEST_CASE("fitter: wls_inverse 權重語意——等權≡反向、w=0 形同剔除、Σw=0 → E-D03",
          "[fitter][wls]") {
  const auto dacs = default_dacs();
  const auto disp = truth_disp(dacs);

  FitOptions inv;
  inv.method = Fitter::ols_inverse;
  FitOptions wls;
  wls.method = Fitter::wls_inverse;

  // 等權(全 1)≡ ols_inverse
  const std::vector<double> ones(dacs.size(), 1.0);
  wls.weights = &ones;
  const auto a = fit_region(dacs, disp, wls);
  const auto b = fit_region(dacs, disp, inv);
  REQUIRE(std::fabs(a.dcc - b.dcc) / b.dcc < 1e-12);
  REQUIRE(std::fabs(a.intercept - b.intercept) < 1e-9);

  // w=0 形同剔除:對兩端點置零 ≡ 把該樣本設 NaN 後之反向(數值等值;n_valid 仍計非 NaN)
  std::vector<double> w0(dacs.size(), 1.0);
  w0.front() = 0.0;
  w0.back() = 0.0;
  wls.weights = &w0;
  const auto c = fit_region(dacs, disp, wls);
  auto disp_nan = disp;
  disp_nan.front() = kNaN;
  disp_nan.back() = kNaN;
  const auto d = fit_region(dacs, disp_nan, inv);
  REQUIRE(std::fabs(c.dcc - d.dcc) / d.dcc < 1e-12);
  REQUIRE(c.n_valid == 10);  // 非 NaN 計數不受權重影響
  REQUIRE(d.n_valid == 8);

  // 全零權重 → E-D03
  const std::vector<double> zeros(dacs.size(), 0.0);
  wls.weights = &zeros;
  try {
    fit_region(dacs, disp, wls);
    FAIL("應拋出 DccError(E-D03)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_D03);
  }
}

TEST_CASE("fitter: deming——δ=0 恆等反向;δ→∞ 收斂前向;無噪還原真值", "[fitter][deming]") {
  const auto dacs = default_dacs();

  // 無噪:還原真值
  FitOptions dm;
  dm.method = Fitter::deming;
  const auto clean = fit_region(dacs, truth_disp(dacs), dm);
  REQUIRE(std::fabs(clean.dcc - kTrueDcc) < 1e-6);
  REQUIRE(std::fabs(clean.intercept - kTrueFocus) < 1e-6);

  // 含噪資料上驗證兩極限(手工可重現之固定擾動,非隨機)
  auto disp = truth_disp(dacs);
  const double perturb[10] = {0.31, -0.24, 0.18, -0.4, 0.05, 0.22, -0.11, 0.36, -0.29, 0.15};
  for (size_t i = 0; i < disp.size(); ++i) disp[i] += perturb[i];

  FitOptions inv;
  inv.method = Fitter::ols_inverse;
  const auto ki = fit_region(dacs, disp, inv);
  dm.deming_delta = 0.0;
  const auto k0 = fit_region(dacs, disp, dm);
  REQUIRE(k0.dcc == ki.dcc);              // δ=0 分支直接走反向公式 → 位元級相等
  REQUIRE(k0.intercept == ki.intercept);

  const auto kf = fit_region(dacs, disp);  // 前向
  dm.deming_delta = 1e12;                  // δ→∞:噪聲全歸 DAC → 前向極限
  const auto kinf = fit_region(dacs, disp, dm);
  REQUIRE(std::fabs(kinf.dcc - kf.dcc) / kf.dcc < 1e-6);

  // 中間 δ:斜率落在前向(小)與反向(大)之間(含噪時 k_fwd < k_inv)
  dm.deming_delta = 100.0;
  const auto kmid = fit_region(dacs, disp, dm);
  REQUIRE(kmid.dcc > kf.dcc);
  REQUIRE(kmid.dcc < ki.dcc);
}

TEST_CASE("fitter: 蒙地卡羅——前向衰減符合理論、反向無偏(UT-F2)", "[fitter][mc]") {
  const auto dacs = default_dacs();
  const auto truth = truth_disp(dacs);
  const double sigma = 2.0;  // 放大噪聲使衰減(−2.3%)遠大於 MC 標準誤(~0.08%)

  // 理論衰減(小樣本精確形):E[Sxx_c] = Sxx_c_true + (n−1)σ² →
  // att = Sx²/(Sx² + σ²·(n−1)/n),Sx² = 真值 disp 之母體變異(n=10 時與
  // 漸近式 Sx²/(Sx²+σ²) 差約 0.25%,斷言容差 0.5% 必須用精確形)
  const double np = static_cast<double>(dacs.size());
  const double att = var_of(truth) / (var_of(truth) + sigma * sigma * (np - 1.0) / np);

  std::mt19937 rng(20260717);
  std::normal_distribution<double> gauss(0.0, sigma);
  FitOptions inv;
  inv.method = Fitter::ols_inverse;

  std::vector<double> k_fwd, k_inv;
  for (int trial = 0; trial < 4000; ++trial) {
    auto disp = truth;
    for (double& x : disp) x += gauss(rng);
    k_fwd.push_back(fit_region(dacs, disp).dcc);
    k_inv.push_back(fit_region(dacs, disp, inv).dcc);
  }
  // 前向:平均斜率 ≈ att·k_true(衰減);反向:≈ k_true(無偏,二階項 <0.3%)
  // 容差 0.008:實測殘差 ~0.53% 為一階 att 公式未涵蓋之二階 Jensen 修正項
  // (比值估計量 E[num/den]≠E[num]/E[den];σ=2、n=10 時理論 ≈ +0.55%,系統性、非 MC 雜訊),
  // 另留 ~3×MC 標準誤(~0.08%)緩衝以耐跨平台 RNG 序列差異(libc++ vs libstdc++)。
  REQUIRE(std::fabs(mean_of(k_fwd) / kTrueDcc - att) < 0.008);
  REQUIRE(std::fabs(mean_of(k_inv) / kTrueDcc - 1.0) < 0.005);
  // 消偏幅度:|反向偏差| 遠小於 |前向偏差|
  REQUIRE(std::fabs(mean_of(k_inv) - kTrueDcc) * 5.0 < std::fabs(mean_of(k_fwd) - kTrueDcc));
}

TEST_CASE("fitter: 蒙地卡羅——誠實 quality 下 WLS 變異優於等權反向(UT-F3)",
          "[fitter][mc][wls]") {
  const auto dacs = default_dacs();
  const auto truth = truth_disp(dacs);
  const double sigma0 = 0.8;

  // 誠實模型(SPEC-004 §3a.1):q = exp(−t²) 隨離焦下降,σ_f = σ₀/√q_f
  std::vector<double> q(dacs.size());
  for (size_t f = 0; f < dacs.size(); ++f) {
    const double t = (static_cast<double>(dacs[f]) - kTrueFocus) / 240.0;
    q[f] = std::exp(-t * t);
  }

  std::mt19937 rng(42);
  std::normal_distribution<double> gauss(0.0, 1.0);
  FitOptions inv;
  inv.method = Fitter::ols_inverse;
  FitOptions wls;
  wls.method = Fitter::wls_inverse;
  wls.weights = &q;  // γ=1:w = q(誠實模型之逆變異最優權重)

  std::vector<double> k_eq, k_w;
  for (int trial = 0; trial < 2000; ++trial) {
    auto disp = truth;
    for (size_t f = 0; f < disp.size(); ++f)
      disp[f] += gauss(rng) * sigma0 / std::sqrt(q[f]);
    k_eq.push_back(fit_region(dacs, disp, inv).dcc);
    k_w.push_back(fit_region(dacs, disp, wls).dcc);
  }
  REQUIRE(var_of(k_w) < 0.95 * var_of(k_eq));                 // 效率增益(保守斷言)
  REQUIRE(std::fabs(mean_of(k_w) / kTrueDcc - 1.0) < 0.01);   // 加權後仍無偏
}

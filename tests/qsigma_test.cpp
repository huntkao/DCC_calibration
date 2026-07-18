// qsigma:由 (q, |殘差|) 樣本回推冪律 σ(q) = σ₀·q^(−p);權重橋接 γ = 2p。
// Sim 誠實模型真值 p = 0.5(σ_eff = σ₀/√q,SPEC-004 §3a.1)。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

#include "dcc_core/error.hpp"
#include "dcc_core/qsigma.hpp"
#include "dcc_core/regression.hpp"
#include "dcc_core/sweep.hpp"

using dcc::DccError;
using dcc::ErrorCode;

TEST_CASE("qsigma: 冪律回推 p≈0.5、σ₀ 誤差<10%(UT-Q1)", "[qsigma]") {
  std::mt19937 rng(7);
  std::normal_distribution<double> gauss(0.0, 1.0);
  std::uniform_real_distribution<double> uq(0.2, 1.0);
  const double sigma0 = 0.5;
  std::vector<double> q, ar;
  for (int i = 0; i < 4000; ++i) {
    const double qi = uq(rng);
    q.push_back(qi);
    ar.push_back(std::fabs(gauss(rng)) * sigma0 / std::sqrt(qi));
  }
  const auto r = dcc::qsigma::calibrate(q, ar);
  REQUIRE(r.p > 0.40);
  REQUIRE(r.p < 0.60);
  REQUIRE(std::fabs(r.sigma0 - sigma0) / sigma0 < 0.10);
  REQUIRE(r.bins_used >= 6);
  REQUIRE(r.r2 > 0.9);

  // 樣本不足 / q 無變異 → E-D03
  try {
    dcc::qsigma::calibrate({0.5, 0.5}, {0.1, 0.2});
    FAIL("應拋出 DccError(E-D03)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_D03);
  }
}

TEST_CASE("qsigma: 標定 γ=2p̂ 餵 WLS 之閉環(UT-Q2)", "[qsigma][closure]") {
  using dcc::regression::Fitter;
  using dcc::regression::FitOptions;
  using dcc::regression::fit_region;

  const auto dacs = dcc::sweep::plan({0, 1023, 220, 620}, {0.1, 0.1, 10});
  const double kTrue = 12.46, fc = 420.0, sigma0 = 0.8;
  std::vector<double> truth(dacs.size()), q(dacs.size());
  for (size_t f = 0; f < dacs.size(); ++f) {
    truth[f] = (static_cast<double>(dacs[f]) - fc) / kTrue;
    const double t = (static_cast<double>(dacs[f]) - fc) / 240.0;
    q[f] = std::exp(-t * t);
  }
  std::mt19937 rng(99);
  std::normal_distribution<double> gauss(0.0, 1.0);
  FitOptions inv;
  inv.method = Fitter::ols_inverse;

  // 槓桿 h_ff = 1/n + (dac_f−d̄)²/Σ_j(dac_j−d̄)²(反向擬合之 regressor 為 DAC)
  const double n = static_cast<double>(dacs.size());
  double dmean = 0.0;
  for (int d : dacs) dmean += static_cast<double>(d);
  dmean /= n;
  double dss = 0.0;
  for (int d : dacs) dss += (static_cast<double>(d) - dmean) * (static_cast<double>(d) - dmean);
  std::vector<double> h(dacs.size());
  for (size_t f = 0; f < dacs.size(); ++f) {
    const double dd = static_cast<double>(dacs[f]) - dmean;
    h[f] = 1.0 / n + dd * dd / dss;
  }

  // 第一段:400 trial 收殘差 → calibrate
  std::vector<double> cq, car;
  for (int trial = 0; trial < 400; ++trial) {
    auto disp = truth;
    for (size_t f = 0; f < disp.size(); ++f) disp[f] += gauss(rng) * sigma0 / std::sqrt(q[f]);
    const auto fit = fit_region(dacs, disp, inv);
    for (size_t f = 0; f < disp.size(); ++f) {
      // disp 殘差:e = x − (y − b)/k(反向擬合之 x̂)。
      // 擬合殘差經 hat matrix 收縮(高槓桿×高 σ 端點最劇),
      // 須除以 √(1−h_ff) 還原;異方差下為一階近似。
      const double e = disp[f] - (static_cast<double>(dacs[f]) - fit.intercept) / fit.dcc;
      cq.push_back(q[f]);
      car.push_back(std::fabs(e) / std::sqrt(1.0 - h[f]));
    }
  }
  const auto cal = dcc::qsigma::calibrate(cq, car);
  REQUIRE(cal.p > 0.35);
  REQUIRE(cal.p < 0.65);
  const double gamma_hat = 2.0 * cal.p;

  // 第二段:γ̂ 權重之 WLS 變異 ≈ 真 γ=1 之 WLS(比值 0.8..1.25)
  std::vector<double> w_hat(q.size()), w_true = q;
  for (size_t f = 0; f < q.size(); ++f) w_hat[f] = std::pow(q[f], gamma_hat);
  FitOptions wh, wt;
  wh.method = wt.method = Fitter::wls_inverse;
  wh.weights = &w_hat;
  wt.weights = &w_true;

  std::vector<double> kh, kt;
  for (int trial = 0; trial < 2000; ++trial) {
    auto disp = truth;
    for (size_t f = 0; f < disp.size(); ++f) disp[f] += gauss(rng) * sigma0 / std::sqrt(q[f]);
    kh.push_back(fit_region(dacs, disp, wh).dcc);
    kt.push_back(fit_region(dacs, disp, wt).dcc);
  }
  auto var_of = [](const std::vector<double>& v) {
    double m = 0.0;
    for (double x : v) m += x;
    m /= static_cast<double>(v.size());
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return s / static_cast<double>(v.size());
  };
  const double ratio = var_of(kh) / var_of(kt);
  REQUIRE(ratio > 0.80);
  REQUIRE(ratio < 1.25);
}

// 輸入單位:q 無量綱 (0,1]、abs_resid = raw_pixel;輸出 sigma0 = raw_pixel、p 無量綱。
// q→σ 殘差標定(設計文件 2026-07-17):由 (q, |殘差|) 樣本回推冪律 σ(q) = σ₀·q^(−p)。
// 程序:等頻分箱 → 每箱 σ̂ = RMS(|e|)(E[e]≈0 時 RMS ≡ std)→ log σ̂ 對 log q̄ OLS。
// 權重橋接:w = 1/σ(q)² ∝ q^(2p) → γ = 2p(Sim 誠實模型真值 p = 0.5 → γ = 1)。
#pragma once

#include <vector>

namespace dcc::qsigma {

struct Result {
  double sigma0 = 0.0;  // σ₀ [raw_pixel]
  double p = 0.0;       // 冪次(→ WLS 權重 γ = 2p)
  double r2 = 0.0;      // log-log 擬合品質
  int bins_used = 0;    // 參與擬合之箱數
};

// q 與 abs_resid 等長;q ≤ 0、非有限值之樣本剔除。
// 失敗:可用箱 < 3 或 log q 無變異 → DccError(E_D03)。
Result calibrate(const std::vector<double>& q, const std::vector<double>& abs_resid,
                 int n_bins = 8);

}  // namespace dcc::qsigma

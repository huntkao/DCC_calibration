# 反向/WLS/EIV fitter + q→σ 標定 實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 為 dcc_core 回歸加入可組態 fitter(反向 OLS / 反向 WLS / Deming)與 q→σ 殘差標定,
以 Sim 蒙地卡羅量化各 fitter 相對前向 OLS 的改善,產出分析報告(**預設行為不變**)。

**Architecture:** options struct + enum 擴充 `regression::fit_region`(方案 A,設計文件
`docs/superpowers/specs/2026-07-17-fitter-wls-eiv-design.md`);新增 core 模組 `qsigma`
(殘差冪律標定,產物 γ=2p 直通 WLS 權重);config/pipeline/GUI/CLI 逐層接線;
CLI `--fitter-scan` 蒙地卡羅掃描產 CSV;實驗結果寫分析報告並同步規格。

**Tech Stack:** C++17、Catch2 v3.8.1(FetchContent)、nlohmann_json、CMake+Ninja。

## Global Constraints(違反 = bug)

- 單位契約:disparity 內部一律 raw_pixel;跨模組函式 docstring 首行標注輸入/輸出單位(鐵律 1)。
- 分層:dcc_core 純 C++(無 I/O/UI/執行緒),依賴單向 gui/cli → app → io → core(鐵律 2)。
- **預設行為 bit-exact 不變**:預設 config(`fitter=ols_forward`)下既有 80 測試全綠、
  前向路徑之浮點運算順序不得改變。
- 錯誤碼沿用具名碼:E_A01 / E_D01 / E_D03 / E_E01(SPEC-001 §3)。
- 文件/註解/log = Traditional Chinese(UTF-8);識別字/commit = English;
  commit 前綴 `M2:`(離線分析/實驗階段),規格改動用 `spec:`。
- 每個 task 完成的最低驗證:`cmake --build build` 零警告 → `ctest --test-dir build` 全綠。
- 蒙地卡羅測試一律固定 seed(確定性;`std::mt19937`)。

---

### Task 1: core — FitOptions + ols_inverse(統一累加器重構)

**Files:**
- Modify: `src/dcc_core/include/dcc_core/regression.hpp`
- Modify: `src/dcc_core/src/regression.cpp`
- Create: `tests/fitter_test.cpp`
- Modify: `tests/CMakeLists.txt`(add_executable 清單加 `fitter_test.cpp`,插在 `ut05_regression_test.cpp` 之後)

**Interfaces:**
- Consumes: 既有 `FitResult`、`DccError`/`ErrorCode`(`dcc_core/error.hpp`)。
- Produces(後續 task 依賴,簽名固定):
  ```cpp
  namespace dcc::regression {
  enum class Fitter { ols_forward, ols_inverse, wls_inverse, deming };
  const char* to_string(Fitter f);  // "ols_forward" 等(config/report/GUI 共用)
  struct FitOptions {
    Fitter method = Fitter::ols_forward;
    const std::vector<double>* weights = nullptr;
    double deming_delta = 0.0;
  };
  FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                       const FitOptions& opts, int min_valid_samples = 8);
  }
  ```

- [ ] **Step 1: 寫失敗測試**

`tests/fitter_test.cpp` 新檔:

```cpp
// fitter 擴充(設計:docs/superpowers/specs/2026-07-17-fitter-wls-eiv-design.md)。
// 座標約定:x = disp [raw_pixel]、y = DAC;DAC 精確、disp 含噪(EIV 前提)。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

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
```

- [ ] **Step 2: 確認測試失敗(編譯錯誤)**

Run: `cmake --build build 2>&1 | tail -5`
Expected: 編譯失敗,`Fitter`/`FitOptions` 未定義。

- [ ] **Step 3: 實作**

`regression.hpp` 在 `FitResult` 之後、`fit_region` 舊宣告之前加入:

```cpp
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

// options 版;舊兩參數版保留且 ≡ ols_forward(位元級不變)。
FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     const FitOptions& opts, int min_valid_samples = 8);
```

`regression.cpp` 全檔改寫為(統一累加器;**前向路徑浮點運算順序與 v1 逐字相同**——
w=1.0 之乘法為恆等、`sw` 累加 1.0 精確等於 n、`b=(sy−k·sx)/sw` 即原式):

```cpp
#include "dcc_core/regression.hpp"

#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::regression {

const char* to_string(Fitter f) {
  switch (f) {
    case Fitter::ols_forward: return "ols_forward";
    case Fitter::ols_inverse: return "ols_inverse";
    case Fitter::wls_inverse: return "wls_inverse";
    case Fitter::deming: return "deming";
  }
  return "?";
}

FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     const FitOptions& opts, int min_valid_samples) {
  if (dacs.size() != disp.size())
    throw DccError(ErrorCode::E_D01, "dacs 與 disp 長度不一致(閉環驗算失敗)");
  if (opts.weights && opts.weights->size() != disp.size())
    throw DccError(ErrorCode::E_D01, "weights 與 disp 長度不一致(閉環驗算失敗)");

  // 加權累加(x = disp [raw_px], y = dac [DAC];非 WLS 時 w ≡ 1.0,與 v1 位元級等價)。
  const bool use_w = (opts.method == Fitter::wls_inverse) && opts.weights;
  double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  int n = 0;
  for (size_t i = 0; i < disp.size(); ++i) {
    if (std::isnan(disp[i])) continue;
    ++n;  // n_valid = 非 NaN 計數(權重不影響;w=0 形同剔除但仍計數)
    double w = 1.0;
    if (use_w) {
      w = (*opts.weights)[i];
      if (!(w > 0.0)) continue;  // w ≤ 0 或 NaN → 不入累加(SPEC-004 §3a.1 q=0 語意)
    }
    const double x = disp[i];
    const double y = static_cast<double>(dacs[i]);
    sw += w; sx += w * x; sy += w * y;
    sxx += w * x * x; sxy += w * x * y; syy += w * y * y;
  }
  if (n < min_valid_samples)
    throw DccError(ErrorCode::E_D03, "區域有效樣本不足:" + std::to_string(n) + " < " +
                                         std::to_string(min_valid_samples));
  if (use_w && sw <= 0.0)
    throw DccError(ErrorCode::E_D03, "WLS 權重總和非正(quality 全零?)");

  const double sxx_c = sxx - sx * sx / sw;  // Σw(x-x̄w)²
  const double sxy_c = sxy - sx * sy / sw;  // Σw(x-x̄w)(y-ȳw)
  const double syy_c = syy - sy * sy / sw;  // Σw(y-ȳw)²

  FitResult r;
  r.n_valid = n;
  switch (opts.method) {
    case Fitter::ols_forward:
      if (sxx_c <= 0.0) throw DccError(ErrorCode::E_D03, "disparity 無變異,無法擬合");
      r.dcc = sxy_c / sxx_c;
      break;
    case Fitter::ols_inverse:
    case Fitter::wls_inverse:
      if (syy_c <= 0.0) throw DccError(ErrorCode::E_D03, "DAC 無變異,無法擬合");
      if (sxy_c == 0.0)
        throw DccError(ErrorCode::E_E01, "共變異為零,斜率未定義,請檢查 LEFT/RIGHT 定義");
      r.dcc = syy_c / sxy_c;
      break;
    case Fitter::deming: {
      if (sxx_c <= 0.0) throw DccError(ErrorCode::E_D03, "disparity 無變異,無法擬合");
      if (syy_c <= 0.0) throw DccError(ErrorCode::E_D03, "DAC 無變異,無法擬合");
      if (sxy_c == 0.0)
        throw DccError(ErrorCode::E_E01, "共變異為零,斜率未定義,請檢查 LEFT/RIGHT 定義");
      const double d = opts.deming_delta;
      if (d == 0.0) {
        r.dcc = syy_c / sxy_c;  // δ=0 = DAC 無噪之極限,恆等反向(閉式解代數極限)
      } else {
        const double t = syy_c - d * sxx_c;
        r.dcc = (t + std::sqrt(t * t + 4.0 * d * sxy_c * sxy_c)) / (2.0 * sxy_c);
      }
      break;
    }
  }
  r.intercept = (sy - r.dcc * sx) / sw;
  r.r2 = (sxx_c > 0.0 && syy_c > 0.0) ? (sxy_c * sxy_c) / (sxx_c * syy_c) : 0.0;

  if (r.dcc <= 0.0)
    throw DccError(ErrorCode::E_E01,
                   "回歸斜率非正(k=" + std::to_string(r.dcc) + "),請檢查 LEFT/RIGHT 定義");
  return r;
}

FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     int min_valid_samples) {
  return fit_region(dacs, disp, FitOptions{}, min_valid_samples);
}

}  // namespace dcc::regression
```

注意:Task 1 就把 deming 分支寫進 switch(避免 Task 3 再動 switch 結構),
但 Task 1 測試只覆蓋 inverse;deming/wls 測試在 Task 2/3。

- [ ] **Step 4: 建置 + 測試全綠(既有 UT-05 亦不得變)**

Run: `cmake --build build && ctest --test-dir build`
Expected: 零警告,82/82 綠(80 舊 + 2 新)。

- [ ] **Step 5: Commit**

```bash
git add src/dcc_core/include/dcc_core/regression.hpp src/dcc_core/src/regression.cpp \
        tests/fitter_test.cpp tests/CMakeLists.txt
git commit -m "M2: regression FitOptions + ols_inverse fitter (unified accumulator, forward path bit-exact)"
```

---

### Task 2: core — wls_inverse 權重語意

**Files:**
- Modify: `tests/fitter_test.cpp`(追加 1 個 TEST_CASE)
- (實作已於 Task 1 併入 switch;本 task 為測試釘死語意,若行為不符再修 `regression.cpp`)

**Interfaces:**
- Consumes: Task 1 之 `FitOptions{method=wls_inverse, weights}`。
- Produces: 權重語意契約——等權 ≡ ols_inverse;w≤0/NaN 形同剔除;Σw≤0 → E_D03。

- [ ] **Step 1: 寫測試(追加到 `tests/fitter_test.cpp` 檔尾)**

```cpp
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
```

- [ ] **Step 2: 建置 + 跑新測試**

Run: `cmake --build build && ./build/tests/dcc_tests "[wls]"`
Expected: PASS(Task 1 實作應已滿足;若 FAIL 依訊息修 `regression.cpp` 後重跑)。

- [ ] **Step 3: 全量測試**

Run: `ctest --test-dir build`
Expected: 83/83 綠。

- [ ] **Step 4: Commit**

```bash
git add tests/fitter_test.cpp
git commit -m "M2: pin wls_inverse weight semantics (equal-weight equivalence, zero-weight exclusion)"
```

---

### Task 3: core — deming 等價性

**Files:**
- Modify: `tests/fitter_test.cpp`(追加 1 個 TEST_CASE)

**Interfaces:**
- Consumes: Task 1 之 `FitOptions{method=deming, deming_delta}`。
- Produces: 契約——δ=0 位元級 ≡ 反向;δ→∞ 收斂前向;無噪還原真值。

- [ ] **Step 1: 寫測試(追加到 `tests/fitter_test.cpp` 檔尾)**

```cpp
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
```

- [ ] **Step 2: 建置 + 跑新測試**

Run: `cmake --build build && ./build/tests/dcc_tests "[deming]"`
Expected: PASS。

- [ ] **Step 3: 全量測試**

Run: `ctest --test-dir build`
Expected: 84/84 綠。

- [ ] **Step 4: Commit**

```bash
git add tests/fitter_test.cpp
git commit -m "M2: pin deming limits (delta=0 == inverse bitwise, delta->inf -> forward)"
```

---

### Task 4: core — 蒙地卡羅消偏與 WLS 效率(UT-F2/F3)

**Files:**
- Modify: `tests/fitter_test.cpp`(追加 2 個 TEST_CASE)

**Interfaces:**
- Consumes: Task 1–3 之四 fitter。
- Produces: 量化證據——前向衰減符合理論式 `att = Sx²/(Sx²+σ²)`;反向無偏;
  誠實 quality 下 WLS 變異 < 等權反向(Task 10 報告引用之核心數字)。

- [ ] **Step 1: 寫測試(追加到 `tests/fitter_test.cpp`;檔頭 include 區補 `#include <random>` 與 `#include <numeric>`)**

```cpp
namespace {
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
  REQUIRE(std::fabs(mean_of(k_fwd) / kTrueDcc - att) < 0.005);
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
```

- [ ] **Step 2: 建置 + 跑新測試**

Run: `cmake --build build && ./build/tests/dcc_tests "[mc]"`
Expected: PASS(固定 seed,結果確定;若 UT-F3 效率斷言 FAIL,先印出兩變異值確認增益量級,
再依實際值放寬至 <0.98——不得反向放寬到無意義)。

- [ ] **Step 3: 全量測試**

Run: `ctest --test-dir build`
Expected: 86/86 綠。

- [ ] **Step 4: Commit**

```bash
git add tests/fitter_test.cpp
git commit -m "M2: Monte Carlo evidence — forward attenuation matches theory, inverse unbiased, WLS variance gain"
```

---

### Task 5: core — qsigma 殘差冪律標定(UT-Q1/Q2)

**Files:**
- Create: `src/dcc_core/include/dcc_core/qsigma.hpp`
- Create: `src/dcc_core/src/qsigma.cpp`
- Modify: `src/dcc_core/CMakeLists.txt`(sources 加 `src/qsigma.cpp`)
- Create: `tests/qsigma_test.cpp`
- Modify: `tests/CMakeLists.txt`(add_executable 清單加 `qsigma_test.cpp`,插在 `fitter_test.cpp` 之後)

**Interfaces:**
- Consumes: `DccError`/`ErrorCode`。
- Produces(Task 9/10 依賴):
  ```cpp
  namespace dcc::qsigma {
  struct Result { double sigma0, p, r2; int bins_used; };
  Result calibrate(const std::vector<double>& q, const std::vector<double>& abs_resid,
                   int n_bins = 8);
  }
  ```

- [ ] **Step 1: 寫失敗測試**

`tests/qsigma_test.cpp` 新檔:

```cpp
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

  // 第一段:400 trial 收殘差 → calibrate
  std::vector<double> cq, car;
  for (int trial = 0; trial < 400; ++trial) {
    auto disp = truth;
    for (size_t f = 0; f < disp.size(); ++f) disp[f] += gauss(rng) * sigma0 / std::sqrt(q[f]);
    const auto fit = fit_region(dacs, disp, inv);
    for (size_t f = 0; f < disp.size(); ++f) {
      // disp 殘差:e = x − (y − b)/k(反向擬合之 x̂)
      const double e = disp[f] - (static_cast<double>(dacs[f]) - fit.intercept) / fit.dcc;
      cq.push_back(q[f]);
      car.push_back(std::fabs(e));
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
```

- [ ] **Step 2: 確認編譯失敗**

Run: `cmake --build build 2>&1 | tail -3`
Expected: `dcc_core/qsigma.hpp` 不存在。

- [ ] **Step 3: 實作**

`src/dcc_core/include/dcc_core/qsigma.hpp`:

```cpp
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
```

`src/dcc_core/src/qsigma.cpp`:

```cpp
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
  std::sort(s.begin(), s.end());

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
```

`src/dcc_core/CMakeLists.txt` sources 清單 `src/regression.cpp` 之後加一行 `src/qsigma.cpp`。

- [ ] **Step 4: 建置 + 全量測試**

Run: `cmake --build build && ctest --test-dir build`
Expected: 零警告,88/88 綠。

- [ ] **Step 5: Commit**

```bash
git add src/dcc_core/include/dcc_core/qsigma.hpp src/dcc_core/src/qsigma.cpp \
        src/dcc_core/CMakeLists.txt tests/qsigma_test.cpp tests/CMakeLists.txt
git commit -m "M2: qsigma residual power-law calibration (sigma(q)=s0*q^-p, gamma=2p bridge)"
```

---

### Task 6: io — config regression 段 + SPEC-004 勘誤

**Files:**
- Modify: `src/dcc_io/include/dcc_io/config.hpp`
- Modify: `src/dcc_io/src/config.cpp`
- Modify: `tests/config_test.cpp`(追加 2 個 TEST_CASE)
- Modify: `specs/SPEC-004_資料格式.md`(§2 config schema 表加 regression 兩列 + 頂部 revision 一行)

**Interfaces:**
- Consumes: Task 1 之 `Fitter`、`to_string(Fitter)`。
- Produces(Task 7/8 依賴):`AppConfig` 新欄位
  ```cpp
  dcc::regression::Fitter fitter = dcc::regression::Fitter::ols_forward;
  double weight_gamma = 1.0;
  ```
  JSON 選填段 `"regression": { "fitter": "...", "weight_gamma": 1.0 }`;
  非法 fitter 字串或 γ ∉ [0,8] → E_A01;serialize 閉環含 regression 段。

- [ ] **Step 1: 寫失敗測試(追加到 `tests/config_test.cpp`;沿用該檔既有 helper/include 風格,若該檔以 `nlohmann::json::parse(dcc::io::default_config_json())` 起手改欄位,照同款寫)**

```cpp
TEST_CASE("config: regression 段——載入、預設、非法值 E-A01", "[config][fitter]") {
  using dcc::regression::Fitter;
  // 預設(段缺省)= ols_forward / γ=1
  const auto c0 = dcc::io::load_config(dcc::io::default_config_json());
  REQUIRE(c0.fitter == Fitter::ols_forward);
  REQUIRE(c0.weight_gamma == 1.0);

  auto j = nlohmann::json::parse(dcc::io::default_config_json());
  j["regression"] = {{"fitter", "wls_inverse"}, {"weight_gamma", 2.0}};
  const auto c1 = dcc::io::load_config(j.dump());
  REQUIRE(c1.fitter == Fitter::wls_inverse);
  REQUIRE(c1.weight_gamma == 2.0);

  j["regression"]["fitter"] = "banana";
  REQUIRE_THROWS_AS(dcc::io::load_config(j.dump()), dcc::DccError);
  j["regression"]["fitter"] = "wls_inverse";
  j["regression"]["weight_gamma"] = 9.0;  // > 8
  REQUIRE_THROWS_AS(dcc::io::load_config(j.dump()), dcc::DccError);
}

TEST_CASE("config: regression 段 serialize 閉環", "[config][fitter]") {
  auto j = nlohmann::json::parse(dcc::io::default_config_json());
  j["regression"] = {{"fitter", "ols_inverse"}, {"weight_gamma", 0.5}};
  const auto c = dcc::io::load_config(j.dump());
  const auto c2 = dcc::io::load_config(dcc::io::serialize_config(c));
  REQUIRE(c2.fitter == c.fitter);
  REQUIRE(c2.weight_gamma == c.weight_gamma);
}
```

(檔頭若缺 `#include "dcc_core/regression.hpp"` 補上。)

- [ ] **Step 2: 確認編譯失敗**(`c0.fitter` 欄位不存在)

- [ ] **Step 3: 實作**

`config.hpp`:include 區加 `#include "dcc_core/regression.hpp"`;
`AppConfig` 之 `// focus` 段前加:

```cpp
  // regression(設計文件 2026-07-17;預設 = v1 行為)
  dcc::regression::Fitter fitter = dcc::regression::Fitter::ols_forward;
  double weight_gamma = 1.0;  // WLS 權重 w = q^γ(僅 wls_inverse 用)
```

`config.cpp` 之 `load_config` 在 `// ── focus / aggregation` 段前加:

```cpp
  // ── regression(選填,含預設)─────────────────────────────────────────
  if (j.contains("regression")) {
    const std::string f = j["regression"].value("fitter", "ols_forward");
    if (f == "ols_forward") c.fitter = dcc::regression::Fitter::ols_forward;
    else if (f == "ols_inverse") c.fitter = dcc::regression::Fitter::ols_inverse;
    else if (f == "wls_inverse") c.fitter = dcc::regression::Fitter::wls_inverse;
    else if (f == "deming") c.fitter = dcc::regression::Fitter::deming;
    else throw DccError(ErrorCode::E_A01, "regression.fitter 非法:" + f);
    c.weight_gamma = j["regression"].value("weight_gamma", 1.0);
    if (c.weight_gamma < 0.0 || c.weight_gamma > 8.0)
      throw DccError(ErrorCode::E_A01, "regression.weight_gamma 須在 0..8");
  }
```

`serialize_config` 在 `j["focus"]...` 之前加:

```cpp
  j["regression"]["fitter"] = dcc::regression::to_string(c.fitter);
  j["regression"]["weight_gamma"] = c.weight_gamma;
```

`default_config_json` 的 `"focus"` 行前加:

```
    "regression": { "fitter": "ols_forward", "weight_gamma": 1.0 },
```

(注意:default JSON 變動會改 config snapshot/hash——無測試釘死 hash 值,
但 Step 4 全量跑確認。)

`specs/SPEC-004_資料格式.md`:§2 config schema 表加兩列:

```
| regression.fitter | enum | "ols_forward"(預設)\| "ols_inverse" \| "wls_inverse" \| "deming";回歸 fitter(設計:docs/superpowers/specs/2026-07-17-fitter-wls-eiv-design.md) |
| regression.weight_gamma | float | WLS 權重 w = q^γ,0..8,預設 1.0(僅 wls_inverse 用;q→σ 標定橋接 γ = 2p) |
```

頂部 revision 加一行:

```
> rev 2026-07-17:§2 新增 regression.fitter / weight_gamma(可組態 fitter;預設 ols_forward 行為不變)。
```

- [ ] **Step 4: 建置 + 全量測試**

Run: `cmake --build build && ctest --test-dir build`
Expected: 90/90 綠(既有 config/IT 案例不得因 default JSON 變動而破)。

- [ ] **Step 5: Commit**

```bash
git add src/dcc_io/include/dcc_io/config.hpp src/dcc_io/src/config.cpp \
        tests/config_test.cpp specs/SPEC-004_資料格式.md
git commit -m "M2: config regression.fitter/weight_gamma + SPEC-004 schema erratum"
```

---

### Task 7: app — pipeline 接線 + report 欄位 + IT

**Files:**
- Modify: `src/dcc_app/src/pipeline.cpp`
- Modify: `src/dcc_app/src/session.cpp`(`build_report_json`)
- Modify: `tests/it_pipeline_test.cpp`(追加 2 個 TEST_CASE)

**Interfaces:**
- Consumes: Task 6 之 `cfg.fitter`/`cfg.weight_gamma`;Task 1 之 options 版 `fit_region`;
  `res.seq.quality`([N][h*w],無 quality 時為空,見 `disp_seq_reader.hpp`)。
- Produces: report.json `result.fitter`(字串)與 `result.weight_gamma`;
  wls 且無 quality → warning「WLS:序列無 quality,退化為等權(≡ ols_inverse)」。

- [ ] **Step 1: 寫失敗測試(追加到 `tests/it_pipeline_test.cpp` 檔尾;該檔已有 `app_cfg()`/`base_spec()`/`kFlatGain` helper 與 nlohmann include)**

```cpp
TEST_CASE("IT: fitter=ols_inverse 全管線 dry-run PASS 且中央 DCC 準確", "[it_fitter]") {
  auto j = nlohmann::json::parse(dcc::io::default_config_json());
  j["regression"] = {{"fitter", "ols_inverse"}};
  const auto cfg = dcc::io::load_config(j.dump());

  auto spec = base_spec(cfg);
  spec.noise_sigma = 0.5;
  spec.seed = 3;
  const auto res = dcc::app::run(cfg, generate(spec), kFlatGain, kFlatGain);
  REQUIRE(res.pass);
  const size_t centers[4] = {2 * 8 + 3, 2 * 8 + 4, 3 * 8 + 3, 3 * 8 + 4};
  double cd = 0.0, truth = 0.0;
  for (size_t idx : centers) cd += res.regions[idx].dcc_raw_px;
  truth += true_dcc(2, 3, 8, 6, 12.46, 14.5) + true_dcc(2, 4, 8, 6, 12.46, 14.5);
  truth += true_dcc(3, 3, 8, 6, 12.46, 14.5) + true_dcc(3, 4, 8, 6, 12.46, 14.5);
  REQUIRE(std::fabs(cd / 4.0 - truth / 4.0) / (truth / 4.0) < 0.03);

  // report 記 fitter/γ(可追溯)
  const auto out = dcc::app::run_session(cfg, generate(spec), "");
  const auto rj = nlohmann::json::parse(out.report_json);
  REQUIRE(rj["result"]["fitter"] == "ols_inverse");
  REQUIRE(rj["result"]["weight_gamma"] == 1.0);
}

TEST_CASE("IT: wls_inverse——有 quality 走加權 PASS;無 quality 退化等權並警告", "[it_fitter]") {
  auto j = nlohmann::json::parse(dcc::io::default_config_json());
  j["regression"] = {{"fitter", "wls_inverse"}, {"weight_gamma", 1.0}};
  const auto cfg = dcc::io::load_config(j.dump());

  auto spec = base_spec(cfg);
  spec.noise_sigma = 0.5;
  spec.seed = 5;
  spec.quality_model = dcc::sim::QualityModel::focus_linked;  // 誠實模型:σ_eff = σ₀/√q
  spec.q_falloff = 0.3;
  const auto with_q = dcc::app::run(cfg, generate(spec), kFlatGain, kFlatGain);
  REQUIRE(with_q.pass);

  spec.quality_model = dcc::sim::QualityModel::off;  // 無 quality 面
  const auto no_q = dcc::app::run(cfg, generate(spec), kFlatGain, kFlatGain);
  REQUIRE(no_q.pass);
  bool warned = false;
  for (const auto& w : no_q.warnings)
    if (w.find("退化為等權") != std::string::npos) warned = true;
  REQUIRE(warned);
}
```

- [ ] **Step 2: 建置並確認新測試 FAIL**(report 無 fitter 欄位、無退化警告)

Run: `cmake --build build && ./build/tests/dcc_tests "[it_fitter]"`
Expected: FAIL。

- [ ] **Step 3: 實作**

`pipeline.cpp` 區域迴圈前(`std::vector<double> intercepts...` 之前)加:

```cpp
  // fitter 組態(設計文件 2026-07-17;預設 ols_forward = v1 行為,位元級不變)。
  dcc::regression::FitOptions fit_opts;
  fit_opts.method = cfg.fitter;
  const bool want_wls = (cfg.fitter == dcc::regression::Fitter::wls_inverse);
  if (want_wls && res.seq.quality.empty())
    res.warnings.push_back("WLS:序列無 quality,退化為等權(≡ ols_inverse)");
```

迴圈內把

```cpp
    const auto fit = dcc::regression::fit_region(res.dacs, disp, cfg.min_valid_samples);
```

改為:

```cpp
    std::vector<double> wts;  // WLS 每幀權重 w = q^γ(q 為聚合後 quality)
    if (want_wls && !res.seq.quality.empty()) {
      wts.resize(n);
      for (size_t f = 0; f < n; ++f)
        wts[f] = std::pow(std::max(res.seq.quality[f][ri], 0.0), cfg.weight_gamma);
      fit_opts.weights = &wts;
    }
    const auto fit = dcc::regression::fit_region(res.dacs, disp, fit_opts, cfg.min_valid_samples);
```

(注意 `fit_opts.weights` 每輪迴圈重新指向本區 `wts`;quality NaN 樣本經 `std::max`
仍為 NaN → core 端視為 w 非正剔除,語意一致。`pipeline.cpp` include 區不需新增——
`regression.hpp` 已在。`<cmath>` 已 include(std::pow)。)

`session.cpp` 之 `build_report_json`,在 `r["pass"] = res.pass;` 之後加:

```cpp
  r["fitter"] = dcc::regression::to_string(cfg.fitter);   // 參數快照(可追溯)
  r["weight_gamma"] = cfg.weight_gamma;
```

(include 區加 `#include "dcc_core/regression.hpp"`。)

- [ ] **Step 4: 建置 + 全量測試**

Run: `cmake --build build && ctest --test-dir build`
Expected: 92/92 綠(預設路徑案例全數不動)。

- [ ] **Step 5: Commit**

```bash
git add src/dcc_app/src/pipeline.cpp src/dcc_app/src/session.cpp tests/it_pipeline_test.cpp
git commit -m "M2: pipeline fitter wiring (config-driven, quality^gamma weights, report traceability)"
```

---

### Task 8: gui — fitter 下拉 + γ 滑桿

**Files:**
- Modify: `src/dcc_gui/panels.cpp`(「判定 / 品質」CollapsingHeader 內、`smooth_limit` 滑桿之後)

**Interfaces:**
- Consumes: Task 6 之 `s.cfg.fitter`/`s.cfg.weight_gamma`;Task 1 之 `to_string(Fitter)`。
- Produces: GUI 可切 fitter/γ(CLI 走 config JSON,能力等價)。

- [ ] **Step 1: 實作(GUI 無單元測試,煙霧驗證)**

`panels.cpp` 在 `ch |= slider_d("smooth_limit", ...)` 之後加:

```cpp
    const char* fitters[] = {"ols_forward", "ols_inverse", "wls_inverse", "deming"};
    int fi = static_cast<int>(s.cfg.fitter);
    if (ImGui::Combo("regression.fitter", &fi, fitters, 4)) {
      s.cfg.fitter = static_cast<dcc::regression::Fitter>(fi);
      ch = true;
    }
    if (s.cfg.fitter == dcc::regression::Fitter::wls_inverse)
      ch |= slider_d("weight_gamma", &s.cfg.weight_gamma, 0.0f, 4.0f, "%.2f");
```

(檔頭 include 若缺 `#include "dcc_core/regression.hpp"` 補上;
`fitters[]` 順序必須與 enum 宣告順序一致——這是 static_cast 的前提。)

- [ ] **Step 2: 建置 + 煙霧**

Run: `cmake --build build && ./build/src/dcc_gui/dcc_gui --smoke && ctest --test-dir build`
Expected: 建置零警告、smoke 正常結束、92/92 綠。

- [ ] **Step 3: Commit**

```bash
git add src/dcc_gui/panels.cpp
git commit -m "M2: GUI fitter combo + weight_gamma slider (CLI/GUI parity)"
```

---

### Task 9: cli — `--fitter-scan` 蒙地卡羅掃描

**Files:**
- Modify: `src/dcc_cli/main.cpp`(dry_run 區塊內、既有 `--scan` 區塊之後,同款風格)

**Interfaces:**
- Consumes: Task 6/7 之 config fitter 欄位(以 `AppConfig` 複本直改欄位)、
  `dcc::sim::generate`/`QualityModel::focus_linked`、`dcc::sim::true_dcc`、
  `dcc::qsigma::calibrate`、`RunResult`(regions/seq.disp/seq.quality/dacs)。
- Produces: stdout CSV(主表 `sigma,fitter,gamma,bias_pct,cv_pct,rmse_pct`;
  空行後附表 `# qsigma` `sigma,p_hat,sigma0_hat,r2,bins`)。

- [ ] **Step 1: 實作**

`main.cpp` include 區加:

```cpp
#include <cmath>
#include <vector>

#include "dcc_core/qsigma.hpp"
#include "dcc_core/regression.hpp"
```

在既有 `--scan` 區塊(`return 0;` 收尾處)之後、`seq_json = dcc::sim::generate(spec);` 之前加:

```cpp
      // fitter 比較掃描(設計文件 2026-07-17):σ × fitter × γ 蒙地卡羅,CSV 輸出。
      if (has_flag(argc, argv, "--fitter-scan")) {
        int seeds = 50;
        if (const char* v = arg_value(argc, argv, "--fs-seeds")) seeds = std::stoi(v);
        const double sigmas[] = {0.2, 0.5, 1.0, 2.0};
        struct Combo { dcc::regression::Fitter f; double gamma; };
        const Combo combos[] = {
            {dcc::regression::Fitter::ols_forward, 1.0},
            {dcc::regression::Fitter::ols_inverse, 1.0},
            {dcc::regression::Fitter::deming, 1.0},
            {dcc::regression::Fitter::wls_inverse, 0.5},
            {dcc::regression::Fitter::wls_inverse, 1.0},
            {dcc::regression::Fitter::wls_inverse, 2.0},
        };
        const std::vector<double> flat(221, 1.0);
        // 真值:逐區 true_dcc;中央 4 區 (2,3)(2,4)(3,3)(3,4) 之平均為「中央」
        const size_t centers[4] = {19, 20, 27, 28};
        double truth_c = 0.0;
        truth_c += dcc::sim::true_dcc(2, 3, 8, 6, 12.46, 14.5) +
                   dcc::sim::true_dcc(2, 4, 8, 6, 12.46, 14.5) +
                   dcc::sim::true_dcc(3, 3, 8, 6, 12.46, 14.5) +
                   dcc::sim::true_dcc(3, 4, 8, 6, 12.46, 14.5);
        truth_c /= 4.0;

        std::string qsig_rows;  // 附表暫存
        std::printf("sigma,fitter,gamma,bias_pct,cv_pct,rmse_pct\n");
        for (double sg : sigmas) {
          // 每 (σ, seed) 只合成一次,跨 combo 配對比較(同資料、不同 fitter)
          std::vector<std::string> seqs;
          for (int sd = 1; sd <= seeds; ++sd) {
            auto sc = spec;
            sc.noise_sigma = sg;
            sc.seed = static_cast<unsigned>(sd);
            sc.quality_model = dcc::sim::QualityModel::focus_linked;  // 誠實:σ_eff=σ₀/√q
            sc.q_falloff = 0.3;
            seqs.push_back(dcc::sim::generate(sc));
          }
          std::vector<double> cal_q, cal_ar;  // qsigma 樣本(取自 ols_inverse 組)
          for (const auto& cb : combos) {
            auto c2 = cfg;
            c2.fitter = cb.f;
            c2.weight_gamma = cb.gamma;
            std::vector<double> centrals;
            double se2 = 0.0;
            size_t nreg = 0;
            for (int sd = 0; sd < seeds; ++sd) {
              const auto res = dcc::app::run(c2, seqs[static_cast<size_t>(sd)], flat, flat);
              double cd = 0.0;
              for (size_t idx : centers) cd += res.regions[idx].dcc_raw_px;
              centrals.push_back(cd / 4.0);
              for (int r = 0; r < 6; ++r)
                for (int cc = 0; cc < 8; ++cc) {
                  const double t = dcc::sim::true_dcc(r, cc, 8, 6, 12.46, 14.5);
                  const double e =
                      (res.regions[static_cast<size_t>(r) * 8 + static_cast<size_t>(cc)]
                           .dcc_raw_px - t) / t;
                  se2 += e * e;
                  ++nreg;
                }
              if (cb.f == dcc::regression::Fitter::ols_inverse) {
                // 殘差收集:e = disp − (dac − b)/k、q 為聚合後 quality
                for (size_t ri = 0; ri < res.regions.size(); ++ri)
                  for (size_t fr = 0; fr < res.seq.disp.size(); ++fr) {
                    const double x = res.seq.disp[fr][ri];
                    const double qv = res.seq.quality[fr][ri];
                    if (std::isnan(x) || std::isnan(qv)) continue;
                    const double e = x - (static_cast<double>(res.dacs[fr]) -
                                          res.regions[ri].intercept) / res.regions[ri].dcc_raw_px;
                    cal_q.push_back(qv);
                    cal_ar.push_back(std::fabs(e));
                  }
              }
            }
            double m = 0.0;
            for (double v : centrals) m += v;
            m /= static_cast<double>(centrals.size());
            double var = 0.0;
            for (double v : centrals) var += (v - m) * (v - m);
            var /= static_cast<double>(centrals.size() - 1);
            std::printf("%.2f,%s,%.1f,%.4f,%.4f,%.4f\n", sg,
                        dcc::regression::to_string(cb.f), cb.gamma,
                        100.0 * (m - truth_c) / truth_c, 100.0 * std::sqrt(var) / m,
                        100.0 * std::sqrt(se2 / static_cast<double>(nreg)));
          }
          const auto cal = dcc::qsigma::calibrate(cal_q, cal_ar);
          char row[128];
          std::snprintf(row, sizeof(row), "%.2f,%.4f,%.4f,%.4f,%d\n", sg, cal.p, cal.sigma0,
                        cal.r2, cal.bins_used);
          qsig_rows += row;
        }
        std::printf("\n# qsigma\nsigma,p_hat,sigma0_hat,r2,bins\n%s", qsig_rows.c_str());
        return 0;
      }
```

- [ ] **Step 2: 建置 + 煙霧執行(小 seeds 快跑)**

Run: `cmake --build build && ./build/src/dcc_cli/dcc_cal --dry-run --fitter-scan --fs-seeds 5 | head -12`
Expected: CSV 標頭 + 各 σ×combo 列,數字合理(σ=0.2 時各 fitter bias ≈ 0;
qsigma 附表 p_hat ≈ 0.5)。

- [ ] **Step 3: 全量測試**

Run: `ctest --test-dir build`
Expected: 92/92 綠。

- [ ] **Step 4: Commit**

```bash
git add src/dcc_cli/main.cpp
git commit -m "M2: dcc_cal --fitter-scan Monte Carlo comparison (CSV + qsigma calibration table)"
```

---

### Task 10: 實驗執行 + 分析報告 + 規格同步

**Files:**
- Create: `docs/fitter實驗_反向WLS與EIV.md`
- Modify: `specs/SPEC-003_架構與模組介面.md`(§5 regression note + 頂部 revision)
- Modify: `specs/SPEC-005_驗證與測試計畫.md`(§8 #2 註記 + 頂部 revision)
- Modify: `docs/開發紀錄_M0-M1.md`(§7 保留待辦 #3 標註完成)
- Modify: `CLAUDE.md`(測試數 80 → 92;M1 骨架區 core 清單補 qsigma)

**Interfaces:**
- Consumes: Task 9 之 `--fitter-scan` CSV 輸出。
- Produces: 分析報告與規格一致性;**結論段必須明確回答「是否建議換預設 fitter」**。

- [ ] **Step 1: 跑正式實驗(50 seeds,約 1 分鐘)**

```bash
./build/src/dcc_cli/dcc_cal --dry-run --fitter-scan > /tmp/fitter_scan.csv
cat /tmp/fitter_scan.csv
```

- [ ] **Step 2: 撰寫分析報告 `docs/fitter實驗_反向WLS與EIV.md`**

結構如下;⟨…⟩ 處填入 Step 1 實測數字(全表照抄 CSV,不得手改數值):

```markdown
# fitter 實驗:反向回歸、WLS 與 EIV(Deming)

> 日期:2026-07-17 · 設計:docs/superpowers/specs/2026-07-17-fitter-wls-eiv-design.md
> 資料:`dcc_cal --dry-run --fitter-scan`(50 seeds,focus_linked 誠實 quality)
> 對應:SPEC-005 §8 #2(EIV 偏差備忘)、SPEC-004 §3a.1(q→σ 標定)

## 1. 問題

回歸 DAC = k·disp + b 中 DAC 精確、disp 含噪 → 前向 OLS 斜率衰減
(理論 att = Sx²/(Sx²+σ²);UT-F2 已驗證理論式吻合)。

## 2. 方法

四 fitter(前向/反向/反向WLS/Deming)× σ ∈ {0.2, 0.5, 1.0, 2.0} × 50 seeds
配對比較(同 seed 同資料);q→σ 殘差標定(qsigma)每 σ 回推 p̂/σ̂₀。

## 3. 結果

### 3.1 主表(bias% / CV% / RMSE%)

⟨CSV 主表照抄,markdown 表格化⟩

### 3.2 qsigma 標定

⟨CSV 附表照抄⟩(Sim 真值 p = 0.5;γ = 2p̂)

### 3.3 判讀

- 前向 OLS 偏差隨 σ² 放大:σ=0.5 → ⟨值⟩%、σ=2.0 → ⟨值⟩%(理論吻合)。
- 反向消偏:σ=2.0 時 bias ⟨值⟩%(vs 前向 ⟨值⟩%)。
- WLS(γ=1)相對等權反向之 CV 改善:⟨值⟩% → ⟨值⟩%。
- γ 錯標靈敏度:γ=0.5/2 之 CV 劣化 ⟨值⟩(結論:對 γ 誤差不敏感/敏感)。
- Deming(δ=0)與反向逐列一致(位元級,UT 釘死)。
- qsigma 回推 p̂ ≈ ⟨值⟩(真值 0.5),σ̂₀ 誤差 ⟨值⟩%——殘差標定程序可用於真實資料。

## 4. 結論與建議

⟨依數字明確回答:典型產線噪聲(§3a 預算 σ≤0.5)下前向偏差 ⟨值⟩% 是否可忽略;
是否建議把預設 fitter 換為 ols_inverse / 條件(何時)啟用 wls_inverse;
真實 SAD quality 到位後的標定步驟(跑 qsigma → γ=2p̂ → 翻 config)⟩

## 5. 重現

`./build/src/dcc_cli/dcc_cal --dry-run --fitter-scan > fitter_scan.csv`
(UT 對應:tests/fitter_test.cpp [fitter][mc]、tests/qsigma_test.cpp [qsigma])
```

- [ ] **Step 3: 規格同步**

- `SPEC-003_架構與模組介面.md` §5 `regression.fit` 之 note 改為:

  ```
  note: fitter 可組態(config regression.fitter):v1 預設 OLS;另有 ols_inverse/
        wls_inverse(w=q^γ)/deming——實驗結論見 docs/fitter實驗_反向WLS與EIV.md
  ```

  頂部 revision 加:`> rev 2026-07-17:§5 regression fitter 由「可抽換」落地為可組態(enum+config),新增 core qsigma 模組。`

- `SPEC-005_驗證與測試計畫.md` §8 #2 段尾加:

  ```
  (2026-07-17 更新:ols_inverse/wls_inverse/deming 已實作並以蒙地卡羅驗證
  ——前向衰減符合理論、反向無偏、WLS 有效;結論與換預設建議見
  docs/fitter實驗_反向WLS與EIV.md。)
  ```

  頂部 revision 加一行同旨。

- `docs/開發紀錄_M0-M1.md` §7「保留(離線分析/實驗)」#3 改為
  `✅ 完成(2026-07-17):q→σ 標定(qsigma)+ WLS/EIV fitter,見 docs/fitter實驗_反向WLS與EIV.md`。

- `CLAUDE.md`:「測試 = Catch2(80 案例全綠)」→ 92(以 ctest 實數為準);
  「跨機器上手」之 `ctest` 註解 80/80 → 92/92;
  M1 骨架 `src/dcc_core/` 清單補 `qsigma`。

- [ ] **Step 4: 最終全量驗證**

```bash
cmake --build build && ctest --test-dir build && \
./build/src/dcc_cli/dcc_cal --dry-run && ./build/src/dcc_gui/dcc_gui --smoke
```
Expected: 零警告、92/92 綠、CLI PASS、smoke OK。

- [ ] **Step 5: Commit**

```bash
git add docs/fitter實驗_反向WLS與EIV.md specs/SPEC-003_架構與模組介面.md \
        specs/SPEC-005_驗證與測試計畫.md docs/開發紀錄_M0-M1.md CLAUDE.md
git commit -m "M2: fitter experiment report + spec sync (SPEC-003/005, dev log, CLAUDE.md)"
```

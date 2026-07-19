# chart 距離公差換算工具 實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 補上「chart 物理距離公差(cm)→ 合焦偏移(DAC)」的轉換模型,與既有 `--scan`(合焦偏移→DCC 誤差)串成雙向換算,讓產線能從「可接受 DCC 誤差」回推「chart 擺放需準到 ±X cm」。

**Architecture:** 新增純函式 core 模組 `chart_dist`(薄透鏡近似 DAC=a+b/dist,兩點標定);CLI 新增 `--chart-tol` 模式串接 `chart_dist` + 既有 synth/pipeline 的中央 4 區 DCC 演算法(正算表 + 反算二分);真實 nl 與 VCM 標定走輸入,程式不內建假裝為真的數字。

**Tech Stack:** C++17、Catch2 v3.8.1(FetchContent)、nlohmann_json、CMake+Ninja。

## Global Constraints(違反 = bug)

- 單位契約:disparity 內部一律 raw_pixel;跨模組函式 docstring 首行標注輸入/輸出單位(鐵律 1)。
- 分層:dcc_core 純 C++(無 I/O/UI/執行緒),依賴單向 gui/cli → app → io → core(鐵律 2)。
- 錯誤碼沿用具名碼:非法輸入一律 `DccError(ErrorCode::E_A01)`(SPEC-001 §3)。
- **誠實性(鐵律 5)**:VCM 標定兩點與 nl 皆為輸入;程式不得內建任何冒充實機量測的數字。示範預設值須在輸出印一行 `# 示範標定值,實機須替換`。`--nl` 無預設(未帶 → exit 3)。
- 文件/註解/log = Traditional Chinese(UTF-8);識別字/commit = English;commit 前綴 `M2:`,規格改動 `spec:`。
- 每個 task 完成最低驗證:`cmake --build build` 零警告 → `ctest --test-dir build` 全綠(現況 92)。
- CLI 結束碼慣例:0=PASS、2=中止、3=參數錯誤。

---

### Task 1: core — chart_dist DAC↔物距模型

**Files:**
- Create: `src/dcc_core/include/dcc_core/chart_dist.hpp`
- Create: `src/dcc_core/src/chart_dist.cpp`
- Modify: `src/dcc_core/CMakeLists.txt`(sources 清單 `src/qsigma.cpp` 之後加 `src/chart_dist.cpp`)
- Create: `tests/chart_dist_test.cpp`
- Modify: `tests/CMakeLists.txt`(add_executable 清單 `qsigma_test.cpp` 之後加 `chart_dist_test.cpp`)

**Interfaces:**
- Consumes: `DccError` / `ErrorCode`(`dcc_core/error.hpp`)。
- Produces(Task 2/3 依賴):
  ```cpp
  namespace dcc::chart_dist {
  struct VcmDistModel { double a; double b; };  // DAC = a + b/dist_cm
  VcmDistModel calibrate_two_point(double dac1, double dist1_cm, double dac2, double dist2_cm);
  double dac_to_dist(const VcmDistModel& m, double dac);       // → 物距 cm
  double dist_to_dac(const VcmDistModel& m, double dist_cm);   // → DAC
  }
  ```

- [ ] **Step 1: 寫失敗測試**

`tests/chart_dist_test.cpp` 新檔:

```cpp
// chart_dist:VCM DAC↔物距薄透鏡近似模型(設計 2026-07-18)。
// DAC = a + b/dist_cm;兩點標定。往返一致、退化輸入 E-A01。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/chart_dist.hpp"
#include "dcc_core/error.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::chart_dist::calibrate_two_point;
using dcc::chart_dist::dac_to_dist;
using dcc::chart_dist::dist_to_dac;

TEST_CASE("chart_dist: 兩點標定 + 往返一致至 1e-9", "[chart_dist]") {
  // 示範標定:INF 220 DAC ≈ 200cm、MACRO 620 DAC ≈ 10cm
  const auto m = calibrate_two_point(220.0, 200.0, 620.0, 10.0);
  // 標定點必須精確還原
  REQUIRE(std::fabs(dist_to_dac(m, 200.0) - 220.0) < 1e-9);
  REQUIRE(std::fabs(dist_to_dac(m, 10.0) - 620.0) < 1e-9);
  // 往返:dac → dist → dac
  for (double dac : {250.0, 420.0, 550.0}) {
    const double d = dac_to_dist(m, dac);
    REQUIRE(d > 0.0);
    REQUIRE(std::fabs(dist_to_dac(m, d) - dac) < 1e-9);
  }
  // 往返:dist → dac → dist
  for (double dist : {15.0, 25.0, 80.0}) {
    const double dac = dist_to_dac(m, dist);
    REQUIRE(std::fabs(dac_to_dist(m, dac) - dist) < 1e-9);
  }
  // 物理性質:DAC 與 1/dist 線性 → 中點 DAC 對應之 1/dist 為兩端 1/dist 平均
  const double dmid = dac_to_dist(m, 420.0);
  REQUIRE(std::fabs(1.0 / dmid - 0.5 * (1.0 / 200.0 + 1.0 / 10.0)) < 1e-9);
}

TEST_CASE("chart_dist: 退化輸入 → E-A01", "[chart_dist][error]") {
  // 兩點 DAC 相同
  REQUIRE_THROWS_AS(calibrate_two_point(300.0, 50.0, 300.0, 20.0), DccError);
  // 兩點物距相同(1/dist 無變異)
  REQUIRE_THROWS_AS(calibrate_two_point(220.0, 50.0, 620.0, 50.0), DccError);
  // 物距 ≤ 0
  REQUIRE_THROWS_AS(calibrate_two_point(220.0, 0.0, 620.0, 10.0), DccError);
  const auto m = calibrate_two_point(220.0, 200.0, 620.0, 10.0);
  REQUIRE_THROWS_AS(dist_to_dac(m, 0.0), DccError);
  REQUIRE_THROWS_AS(dist_to_dac(m, -5.0), DccError);
  // dac 落在 a(物距→∞)→ dac_to_dist 非正,E-A01
  REQUIRE_THROWS_AS(dac_to_dist(m, m.a), DccError);
  try {
    calibrate_two_point(300.0, 50.0, 300.0, 20.0);
    FAIL("應拋出 E-A01");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_A01);
  }
}
```

- [ ] **Step 2: 確認編譯失敗**

Run: `cmake --build build 2>&1 | tail -3`
Expected: `dcc_core/chart_dist.hpp` 不存在。

- [ ] **Step 3: 實作**

`src/dcc_core/include/dcc_core/chart_dist.hpp`:

```cpp
// 輸入單位:dac = DAC(無量綱刻度)、dist_cm = 物距公分(正值)。
// 輸出單位:dac_to_dist → 物距 cm;dist_to_dac → DAC。
// VCM DAC↔物距薄透鏡近似(設計 2026-07-18):DAC = a + b/dist_cm(DAC 與 1/物距 線性)。
// 兩點標定求 a、b(給鏡頭專屬 INF/MACRO 兩個 DAC↔物距對照點)。
#pragma once

namespace dcc::chart_dist {

struct VcmDistModel {
  double a = 0.0;  // 截距(dac 當物距→∞)
  double b = 0.0;  // 斜率(DAC per (1/cm))
};

// 兩點標定。失敗:dac1==dac2、1/dist1==1/dist2、任一 dist ≤ 0 → DccError(E_A01)。
VcmDistModel calibrate_two_point(double dac1, double dist1_cm, double dac2, double dist2_cm);

// DAC → 物距 cm。失敗:結果非正(dac 落在 a 或錯側)→ DccError(E_A01)。
double dac_to_dist(const VcmDistModel& m, double dac);

// 物距 cm → DAC。失敗:dist_cm ≤ 0 → DccError(E_A01)。
double dist_to_dac(const VcmDistModel& m, double dist_cm);

}  // namespace dcc::chart_dist
```

`src/dcc_core/src/chart_dist.cpp`:

```cpp
#include "dcc_core/chart_dist.hpp"

#include "dcc_core/error.hpp"

namespace dcc::chart_dist {

VcmDistModel calibrate_two_point(double dac1, double dist1_cm, double dac2, double dist2_cm) {
  if (dist1_cm <= 0.0 || dist2_cm <= 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:物距須為正");
  if (dac1 == dac2)
    throw DccError(ErrorCode::E_A01, "chart_dist:兩標定點 DAC 相同,無法解斜率");
  const double inv1 = 1.0 / dist1_cm;
  const double inv2 = 1.0 / dist2_cm;
  if (inv1 == inv2)
    throw DccError(ErrorCode::E_A01, "chart_dist:兩標定點物距相同,無法解斜率");
  VcmDistModel m;
  m.b = (dac1 - dac2) / (inv1 - inv2);
  m.a = dac1 - m.b * inv1;
  return m;
}

double dac_to_dist(const VcmDistModel& m, double dac) {
  const double denom = dac - m.a;
  if (denom == 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:dac 對應物距無窮遠");
  const double dist = m.b / denom;
  if (dist <= 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:dac 換算物距非正(超出模型有效域)");
  return dist;
}

double dist_to_dac(const VcmDistModel& m, double dist_cm) {
  if (dist_cm <= 0.0)
    throw DccError(ErrorCode::E_A01, "chart_dist:物距須為正");
  return m.a + m.b / dist_cm;
}

}  // namespace dcc::chart_dist
```

`src/dcc_core/CMakeLists.txt`:`src/qsigma.cpp` 之後加一行 `  src/chart_dist.cpp`。

- [ ] **Step 4: 建置 + 全量測試**

Run: `cmake --build build && ctest --test-dir build`
Expected: 零警告,94/94 綠(92 舊 + 2 新)。

- [ ] **Step 5: Commit**

```bash
git add src/dcc_core/include/dcc_core/chart_dist.hpp src/dcc_core/src/chart_dist.cpp \
        src/dcc_core/CMakeLists.txt tests/chart_dist_test.cpp tests/CMakeLists.txt
git commit -m "M2: chart_dist VCM DAC<->distance model (two-point thin-lens calibration)"
```

---

### Task 2: app 層串接測試 — cm 偏移 → DCC 誤差鏈路(IT-C1)

**Files:**
- Modify: `tests/it_pipeline_test.cpp`(檔尾追加 1 個 TEST_CASE;檔頭若缺 `#include "dcc_core/chart_dist.hpp"` 補上)

**Interfaces:**
- Consumes: Task 1 之 `chart_dist::{calibrate_two_point, dac_to_dist, dist_to_dac}`;
  既有 `app_cfg()`、`base_spec(cfg)`(回傳 `dcc::sim::SynthSpec`,`focus_center` 預設 420、
  `dacs` 由 sweep 規劃)、`kFlatGain`、`dcc::sim::generate`、`dcc::app::run`(回傳 `RunResult`,
  `regions[i].dcc_raw_px`,中央 4 區索引 {19,20,27,28})、`dcc::sim::true_dcc`。
- Produces: 驗證「cm 偏移經 chart_dist 換 DAC → synth focus_center 平移 → 中央 DCC 誤差」
  之單調性與 nl=0 不敏感性;反算二分閉環之演算法正確性。

- [ ] **Step 1: 寫失敗測試(追加到 `tests/it_pipeline_test.cpp` 檔尾)**

```cpp
TEST_CASE("IT-C1: chart 距離公差 → DCC 誤差鏈路(單調 + nl=0 不敏感 + 反算閉環)",
          "[chart_dist][oq3]") {
  const auto cfg = app_cfg();
  // 示範 VCM 標定:INF 220≈200cm、MACRO 620≈10cm
  const auto model = dcc::chart_dist::calibrate_two_point(220.0, 200.0, 620.0, 10.0);
  const double nominal_dac = 420.0;  // 掃描窗中點 = 合焦設計點
  const double nominal_cm = dcc::chart_dist::dac_to_dist(model, nominal_dac);
  REQUIRE(nominal_cm > 0.0);

  // 中央 4 區平均 DCC(相對 Δcm=0 基準)之誤差 %,nl 為傳導係數
  auto central_dcc = [&](double delta_cm, double nl) {
    auto spec = base_spec(cfg);
    spec.nonlinearity = nl;
    const double new_dac = dcc::chart_dist::dist_to_dac(model, nominal_cm + delta_cm);
    spec.focus_center = nominal_dac + (new_dac - nominal_dac);  // = new_dac(顯式表意)
    const auto res = dcc::app::run(cfg, dcc::sim::generate(spec), kFlatGain, kFlatGain);
    double cd = 0.0;
    for (size_t idx : {19u, 20u, 27u, 28u}) cd += res.regions[idx].dcc_raw_px;
    return cd / 4.0;
  };

  // 誤差 %:相對 delta=0 之基準
  auto err_pct = [&](double delta_cm, double nl) {
    const double base = central_dcc(0.0, nl);
    return 100.0 * (central_dcc(delta_cm, nl) - base) / base;
  };

  // (a) nl=0:理想線性,chart 距離公差對 DCC 完全不敏感(開發紀錄 §3.3)
  REQUIRE(std::fabs(err_pct(1.0, 0.0)) < 0.05);
  REQUIRE(std::fabs(err_pct(2.0, 0.0)) < 0.05);

  // (b) nl=0.05:誤差隨 |Δcm| 單調上升
  const double e05 = std::fabs(err_pct(0.5, 0.05));
  const double e10 = std::fabs(err_pct(1.0, 0.05));
  const double e20 = std::fabs(err_pct(2.0, 0.05));
  REQUIRE(e10 > e05);
  REQUIRE(e20 > e10);
  REQUIRE(e20 > 0.05);  // 明顯非零

  // (c) 反算閉環:二分求「誤差達 budget」之 Δcm,代回正算命中 budget(±5% 相對)
  const double nl = 0.05, budget = 1.0;
  double lo = 0.0, hi = 5.0;
  for (int it = 0; it < 50; ++it) {
    const double mid = 0.5 * (lo + hi);
    if (std::fabs(err_pct(mid, nl)) < budget) lo = mid; else hi = mid;
  }
  const double tol_cm = 0.5 * (lo + hi);
  REQUIRE(std::fabs(std::fabs(err_pct(tol_cm, nl)) - budget) < 0.05 * budget);
}
```

- [ ] **Step 2: 建置並確認新測試通過**(Task 1 已提供 chart_dist;此為串接驗證)

Run: `cmake --build build && ./build/tests/dcc_tests "[chart_dist][oq3]"`
Expected: PASS(若單調斷言 flaky,先印各 err_pct 值確認鏈路方向,不得反向放寬到無意義)。

- [ ] **Step 3: 全量測試**

Run: `ctest --test-dir build`
Expected: 95/95 綠。

- [ ] **Step 4: Commit**

```bash
git add tests/it_pipeline_test.cpp
git commit -m "M2: IT chart-distance->DCC chain (monotonic, nl=0 insensitive, reverse-solve closure)"
```

---

### Task 3: CLI — `--chart-tol` 正算表 + 反算公差

**Files:**
- Modify: `src/dcc_cli/main.cpp`(include 區加 `#include "dcc_core/chart_dist.hpp"`;
  dry_run 區塊內、既有 `--fitter-scan` 區塊 `return 0;` 之後、`seq_json = dcc::sim::generate(spec);` 之前插入;檔頭用法註解補一行)

**Interfaces:**
- Consumes: Task 1 之 `chart_dist::*`;既有 `arg_value`/`has_flag`、`spec`(已由 `--nl`/`--focus-center` 等設定)、`dcc::app::run`、中央 4 區 {19,20,27,28}。
- Produces: `--chart-tol` 模式——正算 CSV 表 + 反算 ±cm 公差;缺 `--nl` → exit 3。

- [ ] **Step 1: 實作(插入 `--fitter-scan` 區塊之後)**

檔頭用法註解(`//   dcc_cal --dry-run --fitter-scan ...` 之後)加:
```cpp
//   dcc_cal --dry-run --chart-tol --nl N [--vcm-cal d1:cm1,d2:cm2] [--chart-dist CM]
//                                        [--dcc-budget PCT] [--scan-range-cm CM] [--scan-steps N]
```

插入區塊:
```cpp
      // chart 距離公差 → DCC 靈敏度換算(開放問題 #3;設計 2026-07-18)。
      // cm 偏移 →(chart_dist)→ 合焦偏移 DAC →(synth nl + pipeline)→ 中央 DCC 誤差。
      if (has_flag(argc, argv, "--chart-tol")) {
        if (!arg_value(argc, argv, "--nl")) {
          std::fprintf(stderr,
                       "--chart-tol 需 --nl(實模組非線性量測值,無預設);nl=0 時 DCC 對距離不敏感\n");
          return 3;
        }
        // VCM 兩點標定(示範預設;實機須換)。格式 dac1:cm1,dac2:cm2。
        double d1 = 220.0, c1 = 200.0, d2 = 620.0, c2 = 10.0;
        bool demo_cal = true;
        if (const char* v = arg_value(argc, argv, "--vcm-cal")) {
          demo_cal = false;
          if (std::sscanf(v, "%lf:%lf,%lf:%lf", &d1, &c1, &d2, &c2) != 4) {
            std::fprintf(stderr, "--vcm-cal 格式須為 dac1:cm1,dac2:cm2\n");
            return 3;
          }
        }
        dcc::chart_dist::VcmDistModel model;
        try {
          model = dcc::chart_dist::calibrate_two_point(d1, c1, d2, c2);
        } catch (const dcc::DccError& e) {
          std::fprintf(stderr, "VCM 標定失敗:%s\n", e.what());
          return 3;
        }
        const double nominal_dac = spec.focus_center;  // 掃描窗中點 = 合焦設計點
        double nominal_cm = dcc::chart_dist::dac_to_dist(model, nominal_dac);
        if (const char* v = arg_value(argc, argv, "--chart-dist")) nominal_cm = std::stod(v);
        double budget = 1.0;
        if (const char* v = arg_value(argc, argv, "--dcc-budget")) budget = std::stod(v);
        double range_cm = 3.0;
        int steps = 25;
        if (const char* v = arg_value(argc, argv, "--scan-range-cm")) range_cm = std::stod(v);
        if (const char* v = arg_value(argc, argv, "--scan-steps")) steps = std::stoi(v);

        const std::vector<double> flat(221, 1.0);
        const size_t centers[4] = {19, 20, 27, 28};
        // 中央 DCC(chart 距離 = nominal_cm + delta_cm 時)
        auto central = [&](double delta_cm) -> double {
          auto sc = spec;
          sc.focus_center = dcc::chart_dist::dist_to_dac(model, nominal_cm + delta_cm);
          const auto res = dcc::app::run(cfg, dcc::sim::generate(sc), flat, flat);
          double cd = 0.0;
          for (size_t idx : centers) cd += res.regions[idx].dcc_raw_px;
          return cd / 4.0;
        };
        const double base_dcc = central(0.0);
        auto err_pct = [&](double delta_cm) {
          return 100.0 * (central(delta_cm) - base_dcc) / base_dcc;
        };

        if (demo_cal)
          std::printf("# 示範標定值,實機須替換 --vcm-cal(格式 dac1:cm1,dac2:cm2)\n");
        std::printf("# nominal_cm=%.2f nl=%.4f budget=%.2f%%\n", nominal_cm,
                    spec.nonlinearity, budget);
        // 正算表
        std::printf("dist_cm,delta_cm,offset_dac,central_dcc,delta_dcc_pct\n");
        for (int i = 0; i < steps; ++i) {
          const double dc = -range_cm + 2.0 * range_cm * static_cast<double>(i) /
                                            static_cast<double>(steps - 1);
          const double off = dcc::chart_dist::dist_to_dac(model, nominal_cm + dc) - nominal_dac;
          std::printf("%.3f,%.3f,%.3f,%.6f,%.4f\n", nominal_cm + dc, dc, off,
                      central(dc), err_pct(dc));
        }
        // 反算:二分求 |ΔDCC%| == budget 之單邊 Δcm(靈敏度對 |Δcm| 單調)
        double lo = 0.0, hi = range_cm, tol_cm = -1.0;
        if (std::fabs(err_pct(hi)) >= budget) {
          for (int it = 0; it < 50; ++it) {
            const double mid = 0.5 * (lo + hi);
            if (std::fabs(err_pct(mid)) < budget) lo = mid; else hi = mid;
          }
          tol_cm = 0.5 * (lo + hi);
        }
        std::printf("\n# 反算:中央 DCC 誤差 ≤ %.2f%% 對應之 chart 距離公差\n", budget);
        if (tol_cm >= 0.0)
          std::printf("chart 擺放需準到 ±%.3f cm(標稱 %.2f cm)\n", tol_cm, nominal_cm);
        else
          std::printf("在 ±%.2f cm 範圍內 DCC 誤差皆 < %.2f%%(nl 太小或範圍太窄)\n",
                      range_cm, budget);
        return 0;
      }
```

(注意:`<cstdio>` 之 `std::sscanf` 需 `#include <cstdio>`——main.cpp 已有;`std::fabs` 需 `<cmath>`——已有。)

- [ ] **Step 2: 建置 + 缺 --nl 檢查**

Run: `cmake --build build && ./build/src/dcc_cli/dcc_cal --dry-run --chart-tol; echo "exit=$?"`
Expected: stderr 提示需 --nl,exit=3。

- [ ] **Step 3: 正算 + 反算煙霧**

Run: `./build/src/dcc_cli/dcc_cal --dry-run --chart-tol --nl 0.05 --dcc-budget 1.0`
Expected: 印 `# 示範標定值…` 一行、`# nominal_cm=… nl=0.0500 …`、正算表(delta_cm 由負到正、delta_dcc_pct 隨 |delta_cm| 單調上升、delta_cm=0 列 pct≈0)、反算一行 `chart 擺放需準到 ±X.XXX cm`。nl=0 對照:`--nl 0` 應反算報「範圍內皆 < budget」。

- [ ] **Step 4: 全量測試(CLI 改動不動核心,確認無回歸)**

Run: `ctest --test-dir build && ./build/src/dcc_cli/dcc_cal --dry-run | tail -1`
Expected: 95/95 綠、dry-run PASS。

- [ ] **Step 5: Commit**

```bash
git add src/dcc_cli/main.cpp
git commit -m "M2: dcc_cal --chart-tol (cm tolerance <-> DCC error; forward table + reverse bisection)"
```

---

### Task 4: 分析文件 + 投影片 + 規格同步

**Files:**
- Create: `docs/chart距離公差分析.md`
- Create: `docs/chart公差_投影片.html`
- Modify: `specs/SPEC-005_驗證與測試計畫.md`(§7 #3 更新 + 頂部 revision 一行)
- Modify: `CLAUDE.md`(開放問題 #3 標記工具已落地;測試數 92→95)
- Modify: `docs/開發紀錄_M0-M1.md`(§7 保留待辦 #1 標記完成 + 頂部 revision 一行)

**Interfaces:**
- Consumes: Task 3 之 `--chart-tol` 實測輸出(正算表 + 反算行)。
- Produces: 交付文件與規格一致性;投影片與 fitter 那套同風格。

- [ ] **Step 1: 跑正式輸出取數字**

```bash
./build/src/dcc_cli/dcc_cal --dry-run --chart-tol --nl 0.05 --dcc-budget 1.0 \
  > /tmp/chart_tol_nl05.txt
./build/src/dcc_cli/dcc_cal --dry-run --chart-tol --nl 0.10 --dcc-budget 1.0 \
  > /tmp/chart_tol_nl10.txt
cat /tmp/chart_tol_nl05.txt /tmp/chart_tol_nl10.txt
```

- [ ] **Step 2: 撰寫分析文件 `docs/chart距離公差分析.md`**

⟨…⟩ 處填 Step 1 實測數字(表格照抄,不得手改)。結構:

```markdown
# chart 距離公差 → DCC 靈敏度分析

> 日期:2026-07-18 · 設計:docs/superpowers/specs/2026-07-18-chart-distance-tolerance-design.md
> 工具:dcc_cal --dry-run --chart-tol · 對應:SPEC-005 §7 開放問題 #3

## 1. 問題:兩個獨立誤差源

① disparity 曲線非線性(nl:球差/PD 飽和)把「合焦偏移(DAC)」轉譯成 DCC 誤差——既有 --scan。
② chart 物理距離誤差(cm)→ 合焦偏移(DAC)——本次補上(chart_dist)。
因果:nl=0 時 DCC 對距離公差完全不敏感(理想線性只動截距、不動斜率);nl 是傳導係數。

## 2. 方法

VCM 薄透鏡近似 DAC = a + b/dist_cm,兩點標定(鏡頭專屬 INF/MACRO 對照點)。
鏈路:cm 偏移 →(chart_dist)→ DAC 偏移 →(synth nl + pipeline 中央 4 區)→ DCC 誤差 %。
反算:二分求「誤差達預算」之 ±cm。**真實 nl 與 VCM 標定為輸入;本文示範值須實機替換。**

## 3. 結果(示範標定 220:200cm,620:10cm,標稱 ⟨nominal_cm⟩ cm)

### 3.1 nl=0.05 正算表
⟨CSV 表照抄,markdown 化⟩
反算:中央 DCC 誤差 ≤ 1% → chart 需準到 ±⟨值⟩ cm

### 3.2 nl=0.10
⟨同上⟩

### 3.3 判讀
- nl 越大,同樣 cm 偏移造成的 DCC 誤差越大 → 允許的 chart 公差越嚴。
- nl=0 對照:任意距離公差 ΔDCC≡0(工具自洽性驗證,見 IT-C1)。

## 4. 上線用法(step-by-step)

1. 從實模組量測非線性 nl(外部;球差/PD 響應)。
2. 取該鏡頭 VCM 的兩個 DAC↔物距對照點(INF/MACRO),填 --vcm-cal。
3. 跑 `dcc_cal --dry-run --chart-tol --nl <實測> --vcm-cal <實測> --dcc-budget <允收>`。
4. 讀反算行 → 得治具擺放公差 ±cm,交產線治具規格。

## 5. 限制與開放問題 #3 狀態

換算工具(正/反算 + 兩點標定)已落地;真實 nl 與 VCM 標定待外部量測。
依 2026-07-17「離線+實驗、不碰硬體」定位,真實輸入屬外部承擔——工具備妥即為本專案之關閉條件。

## 6. 重現
`dcc_cal --dry-run --chart-tol --nl 0.05 --dcc-budget 1.0`(UT:tests/chart_dist_test.cpp、tests/it_pipeline_test.cpp [chart_dist])
```

- [ ] **Step 3: 撰寫投影片 `docs/chart公差_投影片.html`**

複製 `docs/fitter實驗_投影片.html` 的 `<style>`、翻頁 `<script>`、`#deck/#stage` 結構
(light 版面、系統字型、`#stage { flex: none; … }`、方向鍵 + 點擊 + hash),換內容為 8 頁:
1. 封面 + 結論(換算工具落地;nl 是傳導係數;真實輸入待外部)
2. 兩個獨立誤差源拆解(①nl 傳導 ②chart 距離,SVG:nl=0 平線 vs nl≠0 斜線)
3. VCM DAC↔物距模型(DAC=a+b/dist 公式 + 兩點標定 SVG:DAC vs 1/dist 直線)
4. 串接鏈路圖(cm →chart_dist→ DAC →synth+pipeline→ ΔDCC%,4 方框 flow)
5. 正算表(nl=0.05,⟨實測數字⟩)
6. 反算 demo(budget 1% → ±X cm;nl 0.05 vs 0.10 對照)
7. step-by-step 上線演示(4 步 + 命令 + 缺 --nl → exit3 等檢查點)
8. 交付與限制(誠實標示示範值;開放問題 #3 狀態)
每頁 SVG 圖表沿用 fitter 投影片的色票(--s-fwd/--s-inv/--s-wls)與 marks 風格。
寫完用瀏覽器逐頁截圖檢查版面(方向鍵翻頁 + resize 觸發)。

- [ ] **Step 4: 規格同步**

- `specs/SPEC-005_驗證與測試計畫.md` §7 開放問題 #3 末尾加:
  ```
  ——**換算工具已落地(2026-07-18)**:core chart_dist(DAC↔物距兩點標定)+ CLI --chart-tol
  (cm 公差↔DCC 誤差正/反算),詳見 docs/chart距離公差分析.md。真實 nl 與 VCM 標定為輸入,
  依離線+實驗定位(不碰硬體)屬外部承擔;工具備妥即本專案關閉條件。
  ```
  頂部 revision 加一行同旨。
- `CLAUDE.md` 開放問題 #3 行改為標示「換算工具已落地(chart_dist + --chart-tol),真實輸入外部承擔」;
  「測試 = Catch2(92 案例全綠)」→ 95(以 ctest 實數為準);「跨機器上手」ctest 註解 92→95;
  M1 骨架 dcc_core 清單補 `chart_dist`。
- `docs/開發紀錄_M0-M1.md` §7「保留(離線分析/實驗)」#1 改為
  `✅ 完成(2026-07-18):chart 距離公差換算工具(chart_dist + --chart-tol),見 docs/chart距離公差分析.md`;
  頂部 revision 加一行。

- [ ] **Step 5: 最終全量驗證**

```bash
cmake --build build && ctest --test-dir build && \
./build/src/dcc_cli/dcc_cal --dry-run && ./build/src/dcc_gui/dcc_gui --smoke
```
Expected: 零警告、95/95 綠、CLI PASS、smoke OK。

- [ ] **Step 6: Commit**

```bash
git add docs/chart距離公差分析.md docs/chart公差_投影片.html \
        specs/SPEC-005_驗證與測試計畫.md CLAUDE.md docs/開發紀錄_M0-M1.md
git commit -m "M2: chart-distance tolerance analysis doc + slides + spec sync (OQ#3 tool closure)"
```

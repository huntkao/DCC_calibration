// IT 整合測試(SPEC-005 §4):Phase A→G 串接,合成序列 dry-run。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "dcc_app/session.hpp"
#include "dcc_core/sweep.hpp"
#include "dcc_io/config.hpp"
#include "dcc_sim/synth.hpp"

namespace fs = std::filesystem;
using dcc::sim::SynthSpec;
using dcc::sim::true_dcc;

namespace {

dcc::io::AppConfig app_cfg() { return dcc::io::load_config(dcc::io::default_config_json()); }

SynthSpec base_spec(const dcc::io::AppConfig& cfg) {
  SynthSpec s;
  s.dacs = dcc::sweep::plan(cfg.vcm, cfg.sweep);
  s.pitch_x = cfg.pitch_x;
  return s;
}

const std::vector<double> kFlatGain(221, 1.0);

fs::path tmp_dir(const char* name) {
  const fs::path p = fs::temp_directory_path() / "dcc_it" / name;
  fs::remove_all(p);
  return p;
}

}  // namespace

TEST_CASE("IT-01: 標準 dry-run(無雜訊)→ PASS、逐區 DCC 誤差 < 3%、全區 r² > 0.99",
          "[it01]") {
  const auto cfg = app_cfg();
  const auto res = dcc::app::run(cfg, generate(base_spec(cfg)), kFlatGain, kFlatGain);

  REQUIRE(res.pass);
  REQUIRE(res.regions.size() == 48);
  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 8; ++c) {
      const auto& reg = res.regions[static_cast<size_t>(r) * 8 + static_cast<size_t>(c)];
      const double truth = true_dcc(r, c, 8, 6, 12.46, 14.5);
      REQUIRE(std::fabs(reg.dcc_raw_px - truth) / truth < 0.03);  // 實際 ~1e-9
      REQUIRE(reg.r2 > 0.99);
      REQUIRE(std::fabs(reg.intercept - 420.0) < 1e-6);
    }
  }
}

TEST_CASE("IT-02: 注入角落無效樣本 → 2 幀剔除仍 PASS;3 幀 → 正確 FAIL(E-D03)", "[it02]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.null_cells = {{0, 5, 7}, {1, 5, 7}};  // 角落區 (r=5,c=7) 前 2 幀
  const auto ok = dcc::app::run_session(cfg, generate(spec), "");
  REQUIRE(ok.completed);
  REQUIRE(ok.pass);

  spec.null_cells.push_back({2, 5, 7});  // 第 3 幀 → 有效 7 < 8
  const auto bad = dcc::app::run_session(cfg, generate(spec), "");
  REQUIRE_FALSE(bad.completed);
  REQUIRE(bad.error_code == "E-D03");
}

TEST_CASE("IT-03: disparity 取負(模擬 L/R 對調)→ E-E01 且現場資料落盤", "[it03]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.center_dcc = -12.46;  // 生成之 disp 全反號 → 斜率為負
  spec.corner_dcc = -14.5;
  const auto dir = tmp_dir("it03");

  const auto out = dcc::app::run_session(cfg, generate(spec), dir.string());
  REQUIRE_FALSE(out.completed);
  REQUIRE(out.error_code == "E-E01");
  REQUIRE(fs::exists(dir / "abort_dump.json"));  // 鐵律 4:先落盤
}

TEST_CASE("IT-04: 合焦 640(> NEAR−step)之序列 → E-F01", "[it04]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.focus_center = 640.0;
  const auto out = dcc::app::run_session(cfg, generate(spec), "");
  REQUIRE_FALSE(out.completed);
  REQUIRE(out.error_code == "E-F01");
}

TEST_CASE("IT-06: 同一序列重跑,報告 bit-exact 一致", "[it06]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.noise_sigma = 0.3;  // 含雜訊亦須確定性(seed 固定)
  spec.seed = 42;
  const std::string seq = generate(spec);

  const auto a = dcc::app::run_session(cfg, seq, "");
  const auto b = dcc::app::run_session(cfg, seq, "");
  REQUIRE(a.completed);
  REQUIRE(a.report_json == b.report_json);  // 逐字元相等
}

TEST_CASE("IT-07: 誤差預算閉環——σ=0.5、bias=0.3 蒙地卡羅×10:中央 CV<2%、偏差<3%",
          "[it07]") {
  const auto cfg = app_cfg();
  // 中央 4 區(r=2..3, c=3..4)之平均 DCC 為「中央 DCC」。
  const size_t centers[4] = {2 * 8 + 3, 2 * 8 + 4, 3 * 8 + 3, 3 * 8 + 4};
  double truth = 0.0;
  truth += true_dcc(2, 3, 8, 6, 12.46, 14.5) + true_dcc(2, 4, 8, 6, 12.46, 14.5);
  truth += true_dcc(3, 3, 8, 6, 12.46, 14.5) + true_dcc(3, 4, 8, 6, 12.46, 14.5);
  truth /= 4.0;

  std::vector<double> central;
  for (unsigned seed = 1; seed <= 10; ++seed) {
    auto spec = base_spec(cfg);
    spec.noise_sigma = 0.5;  // §3a 驗收上限
    spec.bias = 0.3;
    spec.seed = seed;
    const auto res = dcc::app::run(cfg, generate(spec), kFlatGain, kFlatGain);
    double m = 0.0;
    for (size_t idx : centers) m += res.regions[idx].dcc_raw_px;
    central.push_back(m / 4.0);
  }

  double mean = 0.0;
  for (double v : central) mean += v;
  mean /= static_cast<double>(central.size());
  double var = 0.0;
  for (double v : central) var += (v - mean) * (v - mean);
  var /= static_cast<double>(central.size() - 1);
  const double cv = std::sqrt(var) / mean;

  REQUIRE(cv < 0.02);                              // NFR-02
  REQUIRE(std::fabs(mean - truth) / truth < 0.03); // IT-01 準確度(含 bias 情境)
}

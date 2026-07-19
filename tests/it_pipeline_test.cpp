// IT 整合測試(SPEC-005 §4):Phase A→G 串接,合成序列 dry-run。
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <vector>

#include "dcc_app/session.hpp"
#include "dcc_core/chart_dist.hpp"
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

TEST_CASE("OQ#3 靈敏度:非線性 nl=0.05 下,合焦偏移 ±40 DAC 使中央 DCC 反向漂移",
          "[oq3][sensitivity]") {
  // 理論:k_fit ≈ k / (1 − 2·nl·Δ/240) → Δ=+40 時 ΔDCC ≈ +1.7%(nl=0.05)。
  const auto cfg = app_cfg();
  const std::vector<double> flat(221, 1.0);
  const size_t centers[4] = {19, 20, 27, 28};

  const auto central_at = [&](double offset) {
    auto spec = base_spec(cfg);
    spec.nonlinearity = 0.05;
    spec.focus_center = 420.0 + offset;
    const auto res = dcc::app::run(cfg, generate(spec), flat, flat);
    double m = 0.0;
    for (size_t i : centers) m += res.regions[i].dcc_raw_px;
    return m / 4.0;
  };

  const double d0 = central_at(0.0);
  const double dp = 100.0 * (central_at(+40.0) - d0) / d0;
  const double dm = 100.0 * (central_at(-40.0) - d0) / d0;
  REQUIRE(dp > 0.8);   // 正向偏移 → DCC 偏高(約 +1.7%)
  REQUIRE(dp < 3.0);
  REQUIRE(dm < -0.8);  // 反向對稱
  REQUIRE(dm > -3.0);

  // 對照:理想線性(nl=0)時應不敏感(<0.1%)。
  auto lin = base_spec(cfg);
  lin.focus_center = 460.0;
  const auto res_lin = dcc::app::run(cfg, generate(lin), flat, flat);
  double m_lin = 0.0;
  for (size_t i : centers) m_lin += res_lin.regions[i].dcc_raw_px;
  m_lin /= 4.0;
  auto lin0 = base_spec(cfg);
  const auto res_lin0 = dcc::app::run(cfg, generate(lin0), flat, flat);
  double m_lin0 = 0.0;
  for (size_t i : centers) m_lin0 += res_lin0.regions[i].dcc_raw_px;
  m_lin0 /= 4.0;
  REQUIRE(std::fabs(100.0 * (m_lin - m_lin0) / m_lin0) < 0.1);
}

TEST_CASE("FR-15 平滑性:預設門檻無警告;門檻壓低 → 警告列座標且模組仍 PASS", "[fr15]") {
  auto cfg = app_cfg();
  const auto has_smooth_warning = [](const dcc::app::RunResult& r) {
    for (const auto& w : r.warnings)
      if (w.find("平滑性") != std::string::npos) return true;
    return false;
  };

  // 預設梯度(中央 12.46 → 角落 14.5)相鄰差 ~2-4% << 0.25 → 不得誤報。
  const auto res = dcc::app::run(cfg, generate(base_spec(cfg)), kFlatGain, kFlatGain);
  REQUIRE(res.pass);
  REQUIRE_FALSE(has_smooth_warning(res));

  // 門檻壓至 1% → 同一梯度必觸發;FR-15 為警告性質,判定仍 PASS。
  cfg.smooth_limit = 0.01;
  const auto warned = dcc::app::run(cfg, generate(base_spec(cfg)), kFlatGain, kFlatGain);
  REQUIRE(warned.pass);
  REQUIRE(has_smooth_warning(warned));
  bool has_coord = false;
  for (const auto& w : warned.warnings)
    if (w.find("平滑性:區 (") != std::string::npos) has_coord = true;
  REQUIRE(has_coord);
}

TEST_CASE("S 型非線性(nl3):視差對離焦為奇函數;不對稱項(nl2)則否", "[scurve]") {
  const auto cfg = app_cfg();
  // 預設 dacs 對 420 對稱(180+660=840)→ 幀 f 與幀 9−f 之離焦互為相反數。
  const auto disp_at = [&](double nl2, double nl3, size_t frame) {
    auto spec = base_spec(cfg);
    spec.nonlinearity = nl2;
    spec.s_curve = nl3;
    const auto j = nlohmann::json::parse(generate(spec));
    return j["data"][frame][0][0].get<double>();
  };
  // 純 S 型:disp(−u) == −disp(+u)(逐 bit 奇對稱)。
  for (size_t f = 0; f < 5; ++f)
    REQUIRE(disp_at(0.0, 0.2, f) == -disp_at(0.0, 0.2, 9 - f));
  // 純不對稱項:奇對稱必然破壞(鐘型殘差,兩端同向)。
  REQUIRE(disp_at(0.1, 0.0, 0) != -disp_at(0.1, 0.0, 9));
  // S 型端點壓縮:|disp| 小於線性真值(壓縮 nl3×100%)。
  const double lin = disp_at(0.0, 0.0, 0);
  REQUIRE(std::fabs(disp_at(0.0, 0.2, 0)) < std::fabs(lin));
  REQUIRE(std::fabs(disp_at(0.0, 0.2, 0) / lin - 0.8) < 1e-9);  // 端點 un≈1 → ×(1−0.2)
}

TEST_CASE("S 型非線性下 ΔDCC 對合焦偏移為對稱偶函數(vs nl2 之線性反對稱)", "[scurve][oq3]") {
  const auto cfg = app_cfg();
  const std::vector<double> flat(221, 1.0);
  const size_t centers[4] = {19, 20, 27, 28};
  const auto central = [&](double nl3, double offset) {
    auto spec = base_spec(cfg);
    spec.s_curve = nl3;
    spec.focus_center = 420.0 + offset;
    const auto res = dcc::app::run(cfg, generate(spec), flat, flat);
    double m = 0.0;
    for (size_t i : centers) m += res.regions[i].dcc_raw_px;
    return m / 4.0;
  };
  const double d0 = central(0.1, 0.0), dp = central(0.1, +40.0), dm = central(0.1, -40.0);
  REQUIRE(dp > d0);                                  // 壓縮 → 偏移使 DCC 偏高
  REQUIRE(std::fabs(dp - dm) / d0 < 0.002);          // ±40 對稱(偶函數)
}

TEST_CASE("num_positions=12:sweep/synth/管線全鏈連動,真值還原不變", "[nconfig]") {
  auto jcfg = nlohmann::json::parse(dcc::io::default_config_json());
  jcfg["dcc"]["num_positions"] = 12;
  const auto cfg = dcc::io::load_config(jcfg.dump());

  const auto dacs = dcc::sweep::plan(cfg.vcm, cfg.sweep);
  REQUIRE(dacs.size() == 12);
  REQUIRE(dacs.front() == 180);  // 端點公式不受點數影響
  REQUIRE(dacs.back() == 660);

  auto spec = base_spec(cfg);          // spec.dacs 由 plan 供給 → 12 幀
  spec.dacs = dacs;
  spec.noise_sigma = 0.3;
  spec.seed = 5;
  const auto res = dcc::app::run(cfg, generate(spec), kFlatGain, kFlatGain);
  REQUIRE(res.pass);
  REQUIRE(res.seq.disp.size() == 12);  // 幀數全鏈貫通
  REQUIRE(res.regions[0].n_valid == 12);
  const double truth = true_dcc(0, 0, 8, 6, 12.46, 14.5);
  REQUIRE(std::fabs(res.regions[0].dcc_raw_px - truth) / truth < 0.03);
}

TEST_CASE("focus_peak_offset:err == |offset|/span,tolerance 判定正確(FR-14 演練)",
          "[fr14][sensitivity]") {
  const auto cfg = app_cfg();
  const std::vector<double> flat(221, 1.0);

  const auto run_with_offset = [&](double off) {
    auto spec = base_spec(cfg);
    spec.focus_peak_offset = off;
    return dcc::app::run(cfg, generate(spec), flat, flat);
  };

  // offset 48 → err = 48/480 = 0.10 < 0.20 → PASS。
  const auto ok = run_with_offset(48.0);
  REQUIRE(ok.pass);
  REQUIRE(std::fabs(ok.regions[0].err - 0.10) < 0.01);

  // offset 100 → err ≈ 0.208 ≥ 0.20 → 全區 FAIL → 模組 FAIL(不中止,是判定)。
  const auto bad = run_with_offset(100.0);
  REQUIRE_FALSE(bad.pass);
  REQUIRE(bad.regions[0].err > 0.20);
  REQUIRE_FALSE(bad.regions[0].pass);

  // 判定 FAIL 須以 E-F02 記入報告 errors[](SPEC-004 §5)。
  const auto rep = nlohmann::json::parse(dcc::app::build_report_json(cfg, bad));
  REQUIRE(rep["errors"].size() == 1);
  REQUIRE(rep["errors"][0]["code"] == "E-F02");
  const auto rep_ok = nlohmann::json::parse(
      dcc::app::build_report_json(cfg, run_with_offset(48.0)));
  REQUIRE(rep_ok["errors"].empty());
}

TEST_CASE("field_curvature:err 呈徑向圖樣(中央小、角落大),均勻 offset 不會",
          "[fieldcurv][focus]") {
  const auto cfg = app_cfg();
  const std::vector<double> flat(221, 1.0);
  const size_t center = 3 * 8 + 4;  // (r=3,c=4) 近中央
  const size_t corner = 0;          // (r=0,c=0) 角落

  // 場曲 90 DAC:角落 focus 峰值偏 90(err≈0.1875),中央幾乎不偏。
  auto fc_spec = base_spec(cfg);
  fc_spec.field_curvature = 90.0;
  const auto rc = dcc::app::run(cfg, generate(fc_spec), flat, flat);
  REQUIRE(rc.regions[corner].err > rc.regions[center].err + 0.10);  // 徑向落差
  REQUIRE(rc.regions[center].err < 0.05);   // 中央近軸,偏移小(此 grid 最中央區徑向≈0.17)
  REQUIRE(rc.regions[corner].err > 0.15);   // 角落 radial=1 → 90/480≈0.1875

  // 對照:均勻 offset 90 → 全區 err 相近(非徑向)。
  auto off_spec = base_spec(cfg);
  off_spec.focus_peak_offset = 90.0;
  const auto ro = dcc::app::run(cfg, generate(off_spec), flat, flat);
  REQUIRE(std::fabs(ro.regions[corner].err - ro.regions[center].err) < 0.01);

  // 振幅衰減不改峰值位置 → err 不受影響(峰由 argmax 決定)。
  auto amp_spec = base_spec(cfg);
  amp_spec.focus_amp_falloff = 0.7;
  const auto ra = dcc::app::run(cfg, generate(amp_spec), flat, flat);
  REQUIRE(ra.pass);
  REQUIRE(ra.regions[corner].err < 0.02);
}

TEST_CASE("sim::pretty:縮排輸出為合法 JSON 且與原始內容等值", "[sim][pretty]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.quality_model = dcc::sim::QualityModel::constant;
  spec.null_cells = {{0, 0, 0}};  // 含 null 也要等值
  const std::string compact = generate(spec);
  const std::string pretty_text = dcc::sim::pretty(compact);
  REQUIRE(nlohmann::json::parse(pretty_text) == nlohmann::json::parse(compact));
  REQUIRE(pretty_text.find("\n      [") != std::string::npos);  // 每列一行之結構
}

TEST_CASE("sim quality:focus_linked 之 q 在 [0,1]、隨離焦單調遞減、含徑向衰減",
          "[sim][quality]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.quality_model = dcc::sim::QualityModel::focus_linked;
  spec.q_falloff = 0.5;
  const auto j = nlohmann::json::parse(generate(spec));

  REQUIRE(j.contains("quality"));
  REQUIRE(j["quality"].size() == 10);

  // 全值域 [0,1]。
  for (const auto& frame : j["quality"])
    for (const auto& row : frame)
      for (const auto& v : row) {
        REQUIRE(v.get<double>() >= 0.0);
        REQUIRE(v.get<double>() <= 1.0);
      }

  // 單調性(SPEC-004 §3a.1:q 為 σ 之單調遞減代理 → 隨離焦遞減):
  // 合焦 420 落在 f=4(dac≈393)與 f=5 之間 → FAR 端 f=0..4 遞增、NEAR 端 f=5..9 遞減。
  const auto q_at = [&](size_t f, int r, int c) {
    return j["quality"][f][static_cast<size_t>(r)][static_cast<size_t>(c)].get<double>();
  };
  for (size_t f = 0; f < 4; ++f) REQUIRE(q_at(f, 2, 3) < q_at(f + 1, 2, 3));
  for (size_t f = 5; f < 9; ++f) REQUIRE(q_at(f, 2, 3) > q_at(f + 1, 2, 3));

  // 徑向衰減:角落 q < 中央 q(同幀;q_falloff=0.5 → 角落 ×0.5)。
  for (size_t f = 0; f < 10; ++f) REQUIRE(q_at(f, 0, 0) < q_at(f, 2, 3));
}

TEST_CASE("sim quality:q_null_th 造成離焦端點掉樣——th=0.4 恰 2 幀仍 PASS、th=0.6 → E-D03",
          "[sim][quality]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.quality_model = dcc::sim::QualityModel::focus_linked;
  spec.q_falloff = 0.0;  // 全區同 q,掉樣幀數跨區一致

  // th=0.4:僅端點 |t|=1(q=e⁻¹≈0.368)掉樣 → 每區 2 幀 null、8 幀有效 = min_valid_samples。
  spec.q_null_th = 0.4;
  const auto j = nlohmann::json::parse(generate(spec));
  int nulls = 0;
  for (const auto& frame : j["data"])
    for (const auto& row : frame)
      for (const auto& v : row)
        if (v.is_null()) ++nulls;
  REQUIRE(nulls == 2 * 48);                     // 首尾幀 × 48 區
  REQUIRE_FALSE(j["quality"][0][0][0].is_null());  // quality 面仍滿 shape(記錄 q 供追溯)

  const auto ok = dcc::app::run_session(cfg, generate(spec), "");
  REQUIRE(ok.completed);
  REQUIRE(ok.pass);

  // th=0.6:|t|≥0.78 之 4 幀掉樣 → 有效 6 < 8 → E-D03。
  spec.q_null_th = 0.6;
  const auto bad = dcc::app::run_session(cfg, generate(spec), "");
  REQUIRE_FALSE(bad.completed);
  REQUIRE(bad.error_code == "E-D03");
}

TEST_CASE("sim quality:噪聲掛鉤 σ_eff = σ₀/√q——端點幀殘差 std ≈ 中間幀 ×1.64(誠實原則)",
          "[sim][quality]") {
  const auto cfg = app_cfg();
  auto spec = base_spec(cfg);
  spec.quality_model = dcc::sim::QualityModel::focus_linked;
  spec.q_falloff = 0.0;
  spec.noise_sigma = 0.5;
  spec.seed = 7;
  spec.grid_w = 144;  // 細粒度 → 每幀 15552 樣本,std 估計相對誤差 ~0.6%
  spec.grid_h = 108;
  const auto j = nlohmann::json::parse(generate(spec));

  // 殘差 = d − 真值;逐幀樣本 std。
  const auto frame_std = [&](size_t f) {
    const double dac = j["dacs"][f].get<double>();
    double sum = 0.0, sum2 = 0.0;
    int n = 0;
    for (int r = 0; r < 108; ++r)
      for (int c = 0; c < 144; ++c) {
        const double k = true_dcc(r, c, 144, 108, 12.46, 14.5);
        const double res =
            j["data"][f][static_cast<size_t>(r)][static_cast<size_t>(c)].get<double>() -
            (dac - 420.0) / k;
        sum += res;
        sum2 += res * res;
        ++n;
      }
    return std::sqrt(sum2 / n - (sum / n) * (sum / n));
  };

  // f=0:t=−1 → q≈0.368 → σ_eff≈0.824;f=4:t≈−0.11 → q≈0.987 → σ_eff≈0.503。
  const double ratio = frame_std(0) / frame_std(4);
  REQUIRE(ratio > 1.45);
  REQUIRE(ratio < 1.85);
}

TEST_CASE("sim quality:細粒度 + weighted_mean + focus_linked 全管線 → PASS 且 DCC 準",
          "[sim][quality]") {
  auto cfg = app_cfg();
  cfg.agg_method = dcc::aggregate::Method::weighted_mean;  // quality 作 D-5 聚合權重
  auto spec = base_spec(cfg);
  spec.quality_model = dcc::sim::QualityModel::focus_linked;
  spec.q_falloff = 0.3;
  spec.q_null_th = 0.4;  // 端點掉樣 → 聚合端 NaN 傳播亦被運動
  spec.noise_sigma = 0.3;
  spec.seed = 3;
  spec.grid_w = 144;
  spec.grid_h = 108;

  const auto res = dcc::app::run(cfg, generate(spec), kFlatGain, kFlatGain);
  REQUIRE(res.pass);
  for (int r = 0; r < 6; ++r)
    for (int c = 0; c < 8; ++c) {
      const auto& reg = res.regions[static_cast<size_t>(r) * 8 + static_cast<size_t>(c)];
      const double truth = true_dcc(r, c, 8, 6, 12.46, 14.5);  // 區中心近似
      REQUIRE(std::fabs(reg.dcc_raw_px - truth) / truth < 0.05);
    }
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

TEST_CASE("IT: 落盤五檔齊全且 block.json 與 block.bin 等價(checksum/長度閉環)", "[it_out]") {
  const auto cfg = app_cfg();
  const auto dir = tmp_dir("it_out5");
  const auto out = dcc::app::run_session(cfg, generate(base_spec(cfg)), dir.string());
  REQUIRE(out.completed);
  for (const char* f : {"report.json", "report.md", "block.bin", "block.json", "block.txt"})
    REQUIRE(fs::exists(dir / f));

  std::ifstream blk(dir / "block.bin", std::ios::binary);
  const std::vector<char> bin((std::istreambuf_iterator<char>(blk)),
                              std::istreambuf_iterator<char>());
  std::ifstream bj(dir / "block.json");
  const auto j = nlohmann::json::parse(bj);
  REQUIRE(j["total_bytes"].get<size_t>() == bin.size());
  REQUIRE(j["checksum"]["value"].get<int>() ==
          static_cast<int>(static_cast<unsigned char>(bin.back())));
  REQUIRE(j["dcc"]["unit"] == "DAC/raw_pixel");  // config 預設 output_disparity_unit
}

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

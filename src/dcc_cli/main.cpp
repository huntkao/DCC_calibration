// dcc_cal — DCC 校正離線 CLI(解耦試金石:GUI 能做的這裡都能做)。
// 用法:
//   dcc_cal --dry-run [--out DIR] [--sigma S] [--bias B] [--seed N] [--focus-center D]
//   dcc_cal --seq disp_seq.json [--config config.json] [--out DIR]
// 結束碼:0 = PASS、1 = 判定 FAIL、2 = 中止(E-xx)、3 = 參數錯誤。
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "dcc_io/logging.hpp"

#include "dcc_app/pipeline.hpp"
#include "dcc_app/session.hpp"
#include "dcc_core/error.hpp"
#include "dcc_core/qsigma.hpp"
#include "dcc_core/regression.hpp"
#include "dcc_core/sweep.hpp"
#include "dcc_io/config.hpp"
#include "dcc_sim/synth.hpp"

namespace {

const char* arg_value(int argc, char** argv, const char* key) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
  return nullptr;
}

bool has_flag(int argc, char** argv, const char* key) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], key) == 0) return true;
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const bool dry_run = has_flag(argc, argv, "--dry-run");
    const char* seq_path = arg_value(argc, argv, "--seq");
    if (!dry_run && !seq_path) {
      std::fprintf(stderr,
                   "用法:dcc_cal --dry-run [--out DIR] [--sigma S --bias B --seed N "
                   "--focus-center D]\n"
                   "      dcc_cal --seq disp_seq.json [--config config.json] [--out DIR]\n");
      return 3;
    }

    const char* cfg_path = arg_value(argc, argv, "--config");
    const dcc::io::AppConfig cfg = cfg_path ? dcc::io::load_config_file(cfg_path)
                                            : dcc::io::load_config(dcc::io::default_config_json());

    std::string seq_json;
    if (dry_run) {
      dcc::sim::SynthSpec spec;
      spec.dacs = dcc::sweep::plan(cfg.vcm, cfg.sweep);
      spec.pitch_x = cfg.pitch_x;
      spec.unit = cfg.input_disparity_unit;
      if (const char* v = arg_value(argc, argv, "--sigma")) spec.noise_sigma = std::stod(v);
      if (const char* v = arg_value(argc, argv, "--bias")) spec.bias = std::stod(v);
      if (const char* v = arg_value(argc, argv, "--seed")) spec.seed = static_cast<unsigned>(std::stoul(v));
      if (const char* v = arg_value(argc, argv, "--focus-center")) spec.focus_center = std::stod(v);
      if (const char* v = arg_value(argc, argv, "--nl")) spec.nonlinearity = std::stod(v);

      // 靈敏度掃描模式(開放問題 #3):CSV 輸出 offset,central_dcc,delta_pct,max_err,status
      if (has_flag(argc, argv, "--scan")) {
        double range = 60.0;
        int steps = 25;
        if (const char* v = arg_value(argc, argv, "--scan-range")) range = std::stod(v);
        if (const char* v = arg_value(argc, argv, "--scan-steps")) steps = std::stoi(v);
        const std::vector<double> flat(221, 1.0);
        const double base_fc = spec.focus_center;
        double base_dcc = 0.0;
        std::printf("offset_dac,central_dcc,delta_pct,max_err,status\n");
        for (int i = -1; i < steps; ++i) {  // i=-1 為基準點(offset 0)
          const double off = (i < 0) ? 0.0
                                     : -range + 2.0 * range * static_cast<double>(i) /
                                                    static_cast<double>(steps - 1);
          auto sc = spec;
          sc.focus_center = base_fc + off;
          try {
            const auto res = dcc::app::run(cfg, dcc::sim::generate(sc), flat, flat);
            const size_t centers[4] = {19, 20, 27, 28};  // (2,3)(2,4)(3,3)(3,4)
            double cd = 0.0, me = 0.0;
            for (size_t idx : centers) cd += res.regions[idx].dcc_raw_px;
            cd /= 4.0;
            for (const auto& r : res.regions) me = me > r.err ? me : r.err;
            if (i < 0) { base_dcc = cd; continue; }
            std::printf("%.2f,%.6f,%.4f,%.5f,ok\n", off, cd, 100.0 * (cd - base_dcc) / base_dcc, me);
          } catch (const dcc::DccError& e) {
            if (i < 0) { std::fprintf(stderr, "基準點中止:%s\n", e.what()); return 2; }
            std::printf("%.2f,nan,nan,nan,%s\n", off, dcc::to_string(e.code()));
          }
        }
        return 0;
      }

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
                // 殘差收集:e = disp − (dac − b)/k、q 為聚合後 quality。
                // 槓桿修正(Task 5 已確認之計畫勘誤):擬合殘差被 hat matrix 收縮
                // (n=10、2 參數;高槓桿×高 σ 端點最劇),直接餵 calibrate 會使 p̂
                // 系統性低估,須除以 √(1−h_ff) 還原(參考 UT-Q2 同款修正)。
                // h_ff 只依 res.dacs,迴圈外先算好每幀的修正因子。
                const double n = static_cast<double>(res.dacs.size());
                double dmean = 0.0;
                for (int d : res.dacs) dmean += static_cast<double>(d);
                dmean /= n;
                double dss = 0.0;
                for (int d : res.dacs) dss += (static_cast<double>(d) - dmean) * (static_cast<double>(d) - dmean);
                std::vector<double> h(res.dacs.size());
                for (size_t f = 0; f < res.dacs.size(); ++f) {
                  const double dd = static_cast<double>(res.dacs[f]) - dmean;
                  h[f] = 1.0 / n + dd * dd / dss;
                }
                for (size_t ri = 0; ri < res.regions.size(); ++ri)
                  for (size_t fr = 0; fr < res.seq.disp.size(); ++fr) {
                    const double x = res.seq.disp[fr][ri];
                    const double qv = res.seq.quality[fr][ri];
                    if (std::isnan(x) || std::isnan(qv)) continue;
                    const double e = x - (static_cast<double>(res.dacs[fr]) -
                                          res.regions[ri].intercept) / res.regions[ri].dcc_raw_px;
                    cal_q.push_back(qv);
                    cal_ar.push_back(std::fabs(e) / std::sqrt(1.0 - h[fr]));
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

      seq_json = dcc::sim::generate(spec);
      std::printf("[dry-run] 合成序列:σ=%.3f bias=%.3f seed=%u 合焦=%.1f\n", spec.noise_sigma,
                  spec.bias, spec.seed, spec.focus_center);
    } else {
      seq_json.clear();
      FILE* f = std::fopen(seq_path, "rb");
      if (!f) { std::fprintf(stderr, "無法開啟序列檔:%s\n", seq_path); return 3; }
      char buf[65536];
      size_t got;
      while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) seq_json.append(buf, got);
      std::fclose(f);
    }

    const char* out_dir = arg_value(argc, argv, "--out");
    std::unique_ptr<dcc::io::Logger> logger;
    if (out_dir) logger = dcc::io::Logger::create(std::string(out_dir) + "/logs");
    const auto outcome =
        dcc::app::run_session(cfg, seq_json, out_dir ? out_dir : "", logger.get());

    if (!outcome.completed) {
      std::fprintf(stderr, "中止:%s — %s\n", outcome.error_code.c_str(),
                   outcome.error_msg.c_str());
      if (out_dir) std::fprintf(stderr, "現場資料已落盤:%s/abort_dump.json\n", out_dir);
      return 2;
    }
    std::printf("模組判定:%s\n", outcome.pass ? "PASS" : "FAIL");
    if (out_dir)
      std::printf("輸出:%s/report.json、report.md、block.bin、block.json、block.txt\n", out_dir);
    return outcome.pass ? 0 : 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "錯誤:%s\n", e.what());
    return 2;
  }
}

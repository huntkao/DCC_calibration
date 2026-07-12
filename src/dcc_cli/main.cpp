// dcc_cal — DCC 校正離線 CLI(解耦試金石:GUI 能做的這裡都能做)。
// 用法:
//   dcc_cal --dry-run [--out DIR] [--sigma S] [--bias B] [--seed N] [--focus-center D]
//   dcc_cal --seq disp_seq.json [--config config.json] [--out DIR]
// 結束碼:0 = PASS、1 = 判定 FAIL、2 = 中止(E-xx)、3 = 參數錯誤。
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "dcc_io/logging.hpp"

#include "dcc_app/pipeline.hpp"
#include "dcc_app/session.hpp"
#include "dcc_core/error.hpp"
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
      std::printf("輸出:%s/report.json、report.md、block.bin\n", out_dir);
    return outcome.pass ? 0 : 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "錯誤:%s\n", e.what());
    return 2;
  }
}

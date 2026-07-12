#include "gui_state.hpp"

#include <algorithm>

#include "dcc_core/error.hpp"
#include "dcc_core/sweep.hpp"

namespace dcc::gui {

void GuiState::log_add(LogLevel lv, std::string msg) {
  log.push_back({lv, std::move(msg)});
  if (log.size() > 2000) log.erase(log.begin(), log.begin() + 500);  // ring buffer
}

void GuiState::regenerate_and_run() {
  dirty = false;
  has_result = false;
  error_code.clear();
  error_msg.clear();

  try {
    // Sim 參數與 config 對齊(單位/pitch/DAC 序列由 config 推導)。
    spec.dacs = dcc::sweep::plan(cfg.vcm, cfg.sweep);
    spec.pitch_x = cfg.pitch_x;
    spec.unit = cfg.input_disparity_unit;
    if (fine_grid) { spec.grid_w = 144; spec.grid_h = 108; }
    else { spec.grid_w = 8; spec.grid_h = 6; }

    spec.null_cells.clear();
    for (int f = 0; f < null_frames; ++f)
      spec.null_cells.emplace_back(f, spec.grid_h - 1, spec.grid_w - 1);  // 角落區

    last_seq_json = dcc::sim::generate(spec);

    const std::vector<double> flat_gain(221, 1.0);  // SimNvm 透傳
    result = dcc::app::run(cfg, last_seq_json, flat_gain, flat_gain);
    has_result = true;

    log_add(LogLevel::info,
            "管線完成:" + std::string(result.pass ? "PASS" : "FAIL") + ",sweep " +
                std::to_string(result.dacs.front()) + "→" + std::to_string(result.dacs.back()) +
                ",σ=" + std::to_string(spec.noise_sigma) + ",seed=" + std::to_string(spec.seed));
    for (const auto& w : result.warnings) log_add(LogLevel::warn, w);
  } catch (const dcc::DccError& e) {
    error_code = dcc::to_string(e.code());
    error_msg = e.what();
    log_add(LogLevel::error, error_msg);
  } catch (const std::exception& e) {
    error_code = "E-???";
    error_msg = e.what();
    log_add(LogLevel::error, error_msg);
  }
}

void GuiState::run_scan() {
  scan.clear();
  const std::vector<double> flat_gain(221, 1.0);

  // 掃描期間沿用目前 Sim 參數(σ/bias/nl/seed),僅平移合焦位置。
  auto make_point = [&](double offset) {
    ScanPoint p;
    p.offset = offset;
    try {
      auto sc = spec;
      sc.dacs = dcc::sweep::plan(cfg.vcm, cfg.sweep);
      sc.pitch_x = cfg.pitch_x;
      sc.unit = cfg.input_disparity_unit;
      sc.focus_center = spec.focus_center + offset;
      const auto res = dcc::app::run(cfg, dcc::sim::generate(sc), flat_gain, flat_gain);
      const size_t centers[4] = {2 * 8 + 3, 2 * 8 + 4, 3 * 8 + 3, 3 * 8 + 4};
      for (size_t i : centers) p.central_dcc += res.regions[i].dcc_raw_px;
      p.central_dcc /= 4.0;
      for (const auto& r : res.regions) p.max_err = std::max(p.max_err, r.err);
    } catch (const dcc::DccError& e) {
      p.error = dcc::to_string(e.code());
    }
    return p;
  };

  const ScanPoint base = make_point(0.0);
  if (!base.error.empty()) {
    log_add(LogLevel::error, "掃描基準點(offset=0)即中止:" + base.error);
    return;
  }
  for (int i = 0; i < scan_steps; ++i) {
    const double off = -scan_range + 2.0 * scan_range * static_cast<double>(i) /
                                        static_cast<double>(scan_steps - 1);
    ScanPoint p = make_point(off);
    if (p.error.empty())
      p.delta_pct = 100.0 * (p.central_dcc - base.central_dcc) / base.central_dcc;
    scan.push_back(std::move(p));
  }
  int aborted = 0;
  for (const auto& p : scan)
    if (!p.error.empty()) ++aborted;
  log_add(LogLevel::info, "靈敏度掃描完成:" + std::to_string(scan.size()) + " 點(±" +
                              std::to_string(static_cast<int>(scan_range)) + " DAC),中止 " +
                              std::to_string(aborted) + " 點,nl=" +
                              std::to_string(spec.nonlinearity));
}

}  // namespace dcc::gui

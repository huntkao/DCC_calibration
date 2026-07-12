#include "gui_state.hpp"

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

}  // namespace dcc::gui

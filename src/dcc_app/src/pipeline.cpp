#include "dcc_app/pipeline.hpp"

#include <cmath>

#include "dcc_core/eeprom_codec.hpp"
#include "dcc_core/error.hpp"
#include "dcc_core/focus.hpp"
#include "dcc_core/regression.hpp"
#include "dcc_core/sweep.hpp"
#include "dcc_core/units.hpp"

namespace dcc::app {

RunResult run(const dcc::io::AppConfig& cfg, const std::string& disp_seq_json,
              const std::vector<double>& gain_l, const std::vector<double>& gain_r,
              int gain_w, int gain_h) {
  RunResult res;

  // ── Phase C-1:掃描規劃(公式推導;亦為 Phase D 驗證基準)────────────
  res.dacs = dcc::sweep::plan(cfg.vcm, cfg.sweep);
  res.span = static_cast<double>(res.dacs.back() - res.dacs.front());

  // ── Phase D:序列載入與驗證(D-1..D-6)────────────────────────────────
  dcc::io::ReaderConfig rc;
  rc.pitch_x = cfg.pitch_x;
  rc.input_disparity_unit = cfg.input_disparity_unit;
  rc.num_positions = cfg.sweep.num_positions;
  rc.dcc_grid_w = cfg.grid_w;
  rc.dcc_grid_h = cfg.grid_h;
  rc.planned_dacs = res.dacs;
  rc.agg_method = cfg.agg_method;
  rc.min_valid_ratio = cfg.min_valid_ratio;
  rc.min_valid_samples = cfg.min_valid_samples;
  res.seq = dcc::io::load(disp_seq_json, rc);
  res.module_id = res.seq.module_id;

  // ── Phase E:逐區回歸 + Phase F:focus 峰值與交叉驗證 ─────────────────
  const size_t regions = static_cast<size_t>(cfg.grid_w) * static_cast<size_t>(cfg.grid_h);
  const size_t n = res.seq.disp.size();
  res.regions.resize(regions);

  std::vector<double> intercepts(regions), peaks(regions);
  for (size_t ri = 0; ri < regions; ++ri) {
    std::vector<double> disp(n), fv(n);
    for (size_t f = 0; f < n; ++f) { disp[f] = res.seq.disp[f][ri]; fv[f] = res.seq.focus[f][ri]; }

    const auto fit = dcc::regression::fit_region(res.dacs, disp, cfg.min_valid_samples);
    const double peak =
        dcc::focus::peak(res.dacs, fv, cfg.focus_poly_order, cfg.peak_margin_steps);

    auto& r = res.regions[ri];
    r.dcc_raw_px = fit.dcc;
    r.dcc_pd_grid = dcc::units::dcc_raw_px_to_pd_grid(fit.dcc, cfg.pitch_x);
    r.intercept = fit.intercept;
    r.r2 = fit.r2;
    r.n_valid = fit.n_valid;
    r.focus_peak = peak;
    intercepts[ri] = fit.intercept;
    peaks[ri] = peak;

    if (fit.r2 < cfg.r2_warn) {
      const size_t rr = ri / static_cast<size_t>(cfg.grid_w), cc = ri % static_cast<size_t>(cfg.grid_w);
      res.warnings.push_back("區 (r=" + std::to_string(rr) + ", c=" + std::to_string(cc) +
                             ") r²=" + std::to_string(fit.r2) + " < " + std::to_string(cfg.r2_warn));
    }
  }

  const auto judgement = dcc::validate::judge(intercepts, peaks, res.span, cfg.tolerance);
  res.pass = judgement.pass;
  res.worst = judgement.worst;
  for (size_t ri = 0; ri < regions; ++ri) {
    res.regions[ri].err = judgement.err[ri];
    res.regions[ri].pass = judgement.err[ri] < cfg.tolerance;
  }

  // 平滑性檢查(FR-15):相鄰區相對差 > smooth_limit → 警告。
  const auto rel_diff = [](double a, double b) { return std::fabs(a - b) / (0.5 * (a + b)); };
  for (int r = 0; r < cfg.grid_h; ++r) {
    for (int c = 0; c < cfg.grid_w; ++c) {
      const size_t i = static_cast<size_t>(r) * static_cast<size_t>(cfg.grid_w) +
                       static_cast<size_t>(c);
      if (c + 1 < cfg.grid_w && rel_diff(res.regions[i].dcc_raw_px, res.regions[i + 1].dcc_raw_px) > cfg.smooth_limit)
        res.warnings.push_back("平滑性:區 (" + std::to_string(r) + "," + std::to_string(c) +
                               ") 與右鄰差異超過 smooth_limit");
      const size_t below = i + static_cast<size_t>(cfg.grid_w);
      if (r + 1 < cfg.grid_h && rel_diff(res.regions[i].dcc_raw_px, res.regions[below].dcc_raw_px) > cfg.smooth_limit)
        res.warnings.push_back("平滑性:區 (" + std::to_string(r) + "," + std::to_string(c) +
                               ") 與下鄰差異超過 smooth_limit");
    }
  }

  // ── Phase G:打包(輸出端單位轉換,全案出向轉換僅此一處)──────────────
  std::vector<double> dcc_out(regions);
  for (size_t ri = 0; ri < regions; ++ri)
    dcc_out[ri] = (cfg.output_disparity_unit == "pd_image_grid")
                      ? res.regions[ri].dcc_pd_grid
                      : res.regions[ri].dcc_raw_px;
  res.block = dcc::eeprom::pack(gain_l, gain_r, gain_w, gain_h, dcc_out, cfg.grid_w, cfg.grid_h,
                                cfg.q_format);
  if (!dcc::eeprom::verify(res.block))
    throw DccError(ErrorCode::E_G01, "block 回讀驗證失敗");
  return res;
}

}  // namespace dcc::app

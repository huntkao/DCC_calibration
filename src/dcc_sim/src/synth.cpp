#include "dcc_sim/synth.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include <nlohmann/json.hpp>

#include "dcc_core/units.hpp"

namespace dcc::sim {

double true_dcc(int r, int c, int grid_w, int grid_h, double center_dcc, double corner_dcc) {
  // 正規化徑向距離(中心 0 → 角落 1),線性內插。
  const double cy = 0.5 * static_cast<double>(grid_h - 1);
  const double cx = 0.5 * static_cast<double>(grid_w - 1);
  const double dy = (static_cast<double>(r) - cy) / cy;
  const double dx = (static_cast<double>(c) - cx) / cx;
  const double dist = std::min(1.0, std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0));
  return center_dcc + (corner_dcc - center_dcc) * dist;
}

std::string generate(const SynthSpec& s) {
  using nlohmann::json;
  std::mt19937 rng(s.seed);
  std::normal_distribution<double> gauss(0.0, s.noise_sigma);

  const auto is_null = [&](size_t f, int r, int c) {
    for (const auto& [nf, nr, nc] : s.null_cells)
      if (static_cast<size_t>(nf) == f && nr == r && nc == c) return true;
    return false;
  };

  json j;
  j["module_id"] = s.module_id;
  j["unit"] = s.unit;
  j["pitch_x"] = s.pitch_x;
  j["dacs"] = s.dacs;
  j["grid_w"] = s.grid_w;
  j["grid_h"] = s.grid_h;
  j["data"] = json::array();
  j["focus"] = json::array();
  if (s.with_quality) j["quality"] = json::array();

  for (size_t f = 0; f < s.dacs.size(); ++f) {
    const double dac = static_cast<double>(s.dacs[f]);
    json disp_rows = json::array(), focus_rows = json::array(), qual_rows = json::array();
    for (int r = 0; r < s.grid_h; ++r) {
      json drow = json::array(), frow = json::array(), qrow = json::array();
      for (int c = 0; c < s.grid_w; ++c) {
        // focus:以 focus_center 為峰之高斯(全區同型)。
        const double t = (dac - s.focus_center) / 240.0;
        frow.push_back(1000.0 * std::exp(-t * t));
        if (s.with_quality) qrow.push_back(1.0);

        if (is_null(f, r, c)) {
          drow.push_back(nullptr);
          continue;
        }
        const double k = true_dcc(r, c, s.grid_w, s.grid_h, s.center_dcc, s.corner_dcc);
        double d = (dac - s.focus_center) / k + s.bias;
        if (s.noise_sigma > 0.0) d += gauss(rng);
        if (s.unit == "pd_image_grid") d = units::raw_px_to_pd_grid(d, s.pitch_x);
        drow.push_back(d);
      }
      disp_rows.push_back(drow);
      focus_rows.push_back(frow);
      if (s.with_quality) qual_rows.push_back(qrow);
    }
    j["data"].push_back(disp_rows);
    j["focus"].push_back(focus_rows);
    if (s.with_quality) j["quality"].push_back(qual_rows);
  }
  return j.dump();
}

}  // namespace dcc::sim

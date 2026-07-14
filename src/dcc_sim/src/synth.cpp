#include "dcc_sim/synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include <nlohmann/json.hpp>

#include "dcc_core/error.hpp"
#include "dcc_core/units.hpp"

namespace dcc::sim {

double radial_dist(int r, int c, int grid_w, int grid_h) {
  // 中心 0 → 四角 1(對角線正規化)。
  const double cy = 0.5 * static_cast<double>(grid_h - 1);
  const double cx = 0.5 * static_cast<double>(grid_w - 1);
  const double dy = (static_cast<double>(r) - cy) / cy;
  const double dx = (static_cast<double>(c) - cx) / cx;
  return std::min(1.0, std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0));
}

double true_dcc(int r, int c, int grid_w, int grid_h, double center_dcc, double corner_dcc) {
  return center_dcc + (corner_dcc - center_dcc) * radial_dist(r, c, grid_w, grid_h);
}

double true_focus_peak(int r, int c, int grid_w, int grid_h, double focus_center,
                       double peak_offset, double field_curvature) {
  return focus_center + peak_offset + field_curvature * radial_dist(r, c, grid_w, grid_h);
}

std::string generate(const SynthSpec& s) {
  using nlohmann::json;
  if (s.dacs.size() < 2)
    throw DccError(ErrorCode::E_A01, "SynthSpec.dacs 須至少 2 點(應由 sweep::plan 供給)");

  // 正規化尺度 = sweep 半幅(隨 config 之 margin/num_positions 連動,禁止寫死):
  //   非線性項:(dac−fc)/half_span 於掃描端點 ≈ ±1 → nl 語意 = 端點斜率偏離比例
  //   focus 寬度:曲線於掃描端點落至峰值 1/e
  const double half_span = 0.5 * static_cast<double>(s.dacs.back() - s.dacs.front());

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
        // focus:以 true_focus_peak(r,c) 為峰之高斯。
        //  · 均勻 offset(focus_peak_offset)→ b 與 peak 全區同步分歧
        //  · 徑向場曲(field_curvature)→ 各區合焦位置不同 → err 徑向圖樣(實機主導)
        //  · 角落振幅衰減(focus_amp_falloff)→ 離軸 MTF/vignette,僅影響曲線高低外觀
        const double peak_rc = true_focus_peak(r, c, s.grid_w, s.grid_h, s.focus_center,
                                               s.focus_peak_offset, s.field_curvature);
        const double t = (dac - peak_rc) / half_span;
        const double amp =
            1000.0 * (1.0 - s.focus_amp_falloff * radial_dist(r, c, s.grid_w, s.grid_h));
        frow.push_back(amp * std::exp(-t * t));
        if (s.with_quality) qrow.push_back(1.0);

        if (is_null(f, r, c)) {
          drow.push_back(nullptr);
          continue;
        }
        const double k = true_dcc(r, c, s.grid_w, s.grid_h, s.center_dcc, s.corner_dcc);
        const double un = (dac - s.focus_center) / half_span;  // 正規化離焦(端點 ≈ ±1)
        double d = (dac - s.focus_center) / k;
        // nl2 = 偶次不對稱(鐘型殘差)、nl3 = 奇次 S 型壓縮(canonical)。
        if (s.nonlinearity != 0.0 || s.s_curve != 0.0)
          d *= 1.0 + s.nonlinearity * un - s.s_curve * un * un;
        d += s.bias;
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


std::string pretty(const std::string& compact_json) {
  using nlohmann::json;
  const json j = json::parse(compact_json);
  std::string out = "{\n";
  for (const char* k : {"module_id", "unit", "pitch_x", "grid_w", "grid_h"})
    if (j.contains(k)) out += "  \"" + std::string(k) + "\": " + j[k].dump() + ",\n";
  out += "  \"dacs\": " + j["dacs"].dump() + ",\n";

  bool first_plane = true;
  for (const char* key : {"data", "focus", "quality"}) {
    if (!j.contains(key)) continue;
    if (!first_plane) out += ",\n";
    first_plane = false;
    out += "  \"" + std::string(key) + "\": [\n";
    const auto& frames = j[key];
    for (size_t f = 0; f < frames.size(); ++f) {
      out += "    [\n";
      const auto& rows = frames[f];
      for (size_t r = 0; r < rows.size(); ++r)
        out += "      " + rows[r].dump() + (r + 1 < rows.size() ? ",\n" : "\n");
      out += (f + 1 < frames.size()) ? "    ],\n" : "    ]\n";
    }
    out += "  ]";
  }
  out += "\n}\n";
  return out;
}


std::vector<std::uint16_t> generate_raw(const RawSpec& s) {
  // PD 位置查表(單一 pattern 週期)。
  std::vector<std::uint8_t> pd_mask(static_cast<size_t>(s.period_x) * static_cast<size_t>(s.period_y), 0);
  for (const auto& [x, y] : s.pd_left)
    pd_mask[static_cast<size_t>(y) * static_cast<size_t>(s.period_x) + static_cast<size_t>(x)] = 1;
  for (const auto& [x, y] : s.pd_right)
    pd_mask[static_cast<size_t>(y) * static_cast<size_t>(s.period_x) + static_cast<size_t>(x)] = 2;

  std::mt19937 rng(s.seed);
  std::uniform_real_distribution<double> jitter(-3.0, 3.0);

  const double cx = 0.5 * s.width, cy = 0.5 * s.height;
  const double r_norm = 1.0 / (cx * cx + cy * cy);
  std::vector<std::uint16_t> px(static_cast<size_t>(s.width) * static_cast<size_t>(s.height));

  for (int y = 0; y < s.height; ++y) {
    for (int x = 0; x < s.width; ++x) {
      // 垂直線紋理:兩個非諧和弦波混合(SPEC-005 §2 精神)。
      const double t1 = 0.5 + 0.5 * std::sin(2.0 * 3.14159265358979 * x / 37.0);
      const double t2 = 0.5 + 0.5 * std::sin(2.0 * 3.14159265358979 * x / 61.7);
      const double tex = 0.55 * t1 + 0.45 * t2;
      const double dx = x - cx, dy = y - cy;
      const double vig = 1.0 - 0.22 * (dx * dx + dy * dy) * r_norm;  // 輕微 vignette
      double dn = s.black_level + 80.0 + tex * 700.0 * vig + jitter(rng);

      const size_t mi = static_cast<size_t>(y % s.period_y) * static_cast<size_t>(s.period_x) +
                        static_cast<size_t>(x % s.period_x);
      if (pd_mask[mi] != 0)  // metal-shield 遮光
        dn = s.black_level + (dn - s.black_level) * s.pd_attenuation;

      if (dn < 0.0) dn = 0.0;
      if (dn > 1023.0) dn = 1023.0;
      px[static_cast<size_t>(y) * static_cast<size_t>(s.width) + static_cast<size_t>(x)] =
          static_cast<std::uint16_t>(std::lround(dn));
    }
  }
  return px;
}

}  // namespace dcc::sim

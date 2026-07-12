// 測試共用:依 SPEC-004 §3a 建構 disp_seq JSON(合成真值,SPEC-005 §2)。
#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dcc_core/sweep.hpp"
#include "dcc_io/disp_seq_reader.hpp"

namespace testutil {

inline constexpr double kTrueDcc = 12.46;
inline constexpr double kTrueFocusDac = 420.0;

inline std::vector<int> default_dacs() {
  return dcc::sweep::plan({0, 1023, 220, 620}, {0.1, 0.1, 10});
}

// 合成真值:disp = (dac − 420) / true_dcc [raw_pixel]。
inline double truth_disp_raw_px(int dac) {
  return (static_cast<double>(dac) - kTrueFocusDac) / kTrueDcc;
}

// 合成 focus:以 420 為峰之高斯。
inline double truth_focus(int dac) {
  const double t = (static_cast<double>(dac) - kTrueFocusDac) / 240.0;
  return 1000.0 * std::exp(-t * t);
}

struct SeqSpec {
  std::string module_id = "TEST0001";
  std::string unit = "raw_pixel";
  int pitch_x = 16;
  int grid_w = 8, grid_h = 6;
  std::vector<int> dacs = default_dacs();
  // 回傳 NaN → 該 cell 輸出 null。
  std::function<double(size_t f, int r, int c)> disp =
      [](size_t f, int, int) { return truth_disp_raw_px(default_dacs()[f]); };
  std::function<double(size_t f, int r, int c)> focus =
      [](size_t f, int, int) { return truth_focus(default_dacs()[f]); };
  bool with_quality = false;
  std::function<double(size_t f, int r, int c)> quality = [](size_t, int, int) { return 1.0; };
};

inline nlohmann::json make_seq_json(const SeqSpec& s) {
  using nlohmann::json;
  const auto plane = [&](const std::function<double(size_t, int, int)>& fn, size_t f) {
    json rows = json::array();
    for (int r = 0; r < s.grid_h; ++r) {
      json row = json::array();
      for (int c = 0; c < s.grid_w; ++c) {
        const double v = fn(f, r, c);
        if (std::isnan(v)) row.push_back(nullptr);
        else row.push_back(v);
      }
      rows.push_back(row);
    }
    return rows;
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
    j["data"].push_back(plane(s.disp, f));
    j["focus"].push_back(plane(s.focus, f));
    if (s.with_quality) j["quality"].push_back(plane(s.quality, f));
  }
  return j;
}

inline dcc::io::ReaderConfig default_reader_cfg() {
  dcc::io::ReaderConfig cfg;
  cfg.pitch_x = 16;
  cfg.input_disparity_unit = "raw_pixel";
  cfg.num_positions = 10;
  cfg.dcc_grid_w = 8;
  cfg.dcc_grid_h = 6;
  cfg.planned_dacs = default_dacs();
  return cfg;
}

}  // namespace testutil

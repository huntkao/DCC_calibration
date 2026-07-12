#include "dcc_io/disp_seq_reader.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "dcc_core/error.hpp"
#include "dcc_core/units.hpp"

namespace dcc::io {

namespace {

using nlohmann::json;

// 解析單一幀之 [grid_h][grid_w] 數值面;null → NaN。形狀不符 → E_D01。
std::vector<double> parse_plane(const json& frame, int grid_w, int grid_h,
                                const char* field_name) {
  if (!frame.is_array() || frame.size() != static_cast<size_t>(grid_h))
    throw DccError(ErrorCode::E_D01,
                   std::string(field_name) + " 幀列數與 grid_h 不符(閉環驗算失敗)");
  std::vector<double> plane;
  plane.reserve(static_cast<size_t>(grid_w) * static_cast<size_t>(grid_h));
  for (const auto& row : frame) {
    if (!row.is_array() || row.size() != static_cast<size_t>(grid_w))
      throw DccError(ErrorCode::E_D01,
                     std::string(field_name) + " 幀行數與 grid_w 不符(閉環驗算失敗)");
    for (const auto& v : row) {
      if (v.is_null()) plane.push_back(std::nan(""));
      else if (v.is_number()) plane.push_back(v.get<double>());
      else throw DccError(ErrorCode::E_D01, std::string(field_name) + " 含非數值/非 null 元素");
    }
  }
  return plane;
}

const json& require_field(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end())
    throw DccError(ErrorCode::E_D01, std::string("disp_seq 缺必填欄位:") + key);
  return *it;
}

}  // namespace

DispSeq load(const std::string& json_text, const ReaderConfig& cfg) {
  json j;
  try {
    j = json::parse(json_text);
  } catch (const json::parse_error& e) {
    throw DccError(ErrorCode::E_D01, std::string("disp_seq JSON 解析失敗:") + e.what());
  }

  DispSeq seq;
  seq.module_id = require_field(j, "module_id").get<std::string>();

  // ── D-3(前半):unit / pitch_x 與 config 一致 ─────────────────────────
  const std::string unit = require_field(j, "unit").get<std::string>();
  if (unit != "raw_pixel" && unit != "pd_image_grid")
    throw DccError(ErrorCode::E_D02, "unit 非法:" + unit);
  if (unit != cfg.input_disparity_unit)
    throw DccError(ErrorCode::E_D02,
                   "unit(" + unit + ")與 config input_disparity_unit(" +
                       cfg.input_disparity_unit + ")不一致");
  const int pitch_x = require_field(j, "pitch_x").get<int>();
  if (pitch_x != cfg.pitch_x)
    throw DccError(ErrorCode::E_D02, "pitch_x(" + std::to_string(pitch_x) + ")與 config(" +
                                         std::to_string(cfg.pitch_x) + ")不一致");

  // ── D-1:形狀驗證 ────────────────────────────────────────────────────
  const int in_w = require_field(j, "grid_w").get<int>();
  const int in_h = require_field(j, "grid_h").get<int>();
  if (in_w <= 0 || in_h <= 0) throw DccError(ErrorCode::E_D01, "grid_w/grid_h 非法");

  for (const auto& d : require_field(j, "dacs")) seq.dacs.push_back(d.get<int>());
  const size_t n = seq.dacs.size();
  if (n != static_cast<size_t>(cfg.num_positions))
    throw DccError(ErrorCode::E_D01, "dacs 長度(" + std::to_string(n) + ")!= num_positions(" +
                                         std::to_string(cfg.num_positions) + ")");

  const json& data = require_field(j, "data");
  const json& focus = require_field(j, "focus");
  const bool has_quality = j.contains("quality") && !j["quality"].is_null();
  if (data.size() != n || focus.size() != n || (has_quality && j["quality"].size() != n))
    throw DccError(ErrorCode::E_D01, "data/focus/quality 幀數與 dacs 不一致(閉環驗算失敗)");

  // ── D-2:DAC 嚴格遞增(鐵律 3)且與 C-1 規劃一致 ─────────────────────
  for (size_t i = 1; i < n; ++i)
    if (seq.dacs[i] <= seq.dacs[i - 1])
      throw DccError(ErrorCode::E_D02, "dacs 非嚴格遞增(FAR→NEAR 單向,鐵律 3)");
  if (!cfg.planned_dacs.empty()) {
    if (cfg.planned_dacs.size() != n)
      throw DccError(ErrorCode::E_D02, "dacs 長度與 C-1 規劃不一致");
    for (size_t i = 0; i < n; ++i)
      if (std::abs(seq.dacs[i] - cfg.planned_dacs[i]) > 1)
        throw DccError(ErrorCode::E_D02,
                       "dacs[" + std::to_string(i) + "]=" + std::to_string(seq.dacs[i]) +
                           " 與規劃 " + std::to_string(cfg.planned_dacs[i]) + " 差 > 1 DAC");
  }

  // ── D-4 + D-3(後半)+ D-5:逐幀解析、單位轉換、聚合 ─────────────────
  const aggregate::GridSize in_g{in_w, in_h};
  const aggregate::GridSize out_g{cfg.dcc_grid_w, cfg.dcc_grid_h};
  seq.grid_w = cfg.dcc_grid_w;
  seq.grid_h = cfg.dcc_grid_h;

  for (size_t f = 0; f < n; ++f) {
    auto disp_cells = parse_plane(data[f], in_w, in_h, "data");
    auto focus_cells = parse_plane(focus[f], in_w, in_h, "focus");
    std::vector<double> qual_cells;
    if (has_quality) qual_cells = parse_plane(j["quality"][f], in_w, in_h, "quality");

    // 單位轉換:pd_image_grid → raw_pixel(全案入向轉換僅此一處)。
    if (unit == "pd_image_grid")
      for (double& v : disp_cells)
        if (!std::isnan(v)) v = units::pd_grid_to_raw_px(v, pitch_x);

    const std::vector<double>* qp = has_quality ? &qual_cells : nullptr;
    seq.disp.push_back(
        aggregate::aggregate(disp_cells, in_g, out_g, cfg.agg_method, qp, cfg.min_valid_ratio));
    seq.focus.push_back(
        aggregate::aggregate(focus_cells, in_g, out_g, cfg.agg_method, qp, cfg.min_valid_ratio));
    if (has_quality)
      seq.quality.push_back(aggregate::aggregate(qual_cells, in_g, out_g,
                                                 aggregate::Method::weighted_mean, nullptr,
                                                 cfg.min_valid_ratio));
  }

  // ── D-6:逐區跨幀有效樣本統計 ────────────────────────────────────────
  const size_t regions = static_cast<size_t>(cfg.dcc_grid_w) * static_cast<size_t>(cfg.dcc_grid_h);
  for (size_t ri = 0; ri < regions; ++ri) {
    int valid = 0;
    for (size_t f = 0; f < n; ++f)
      if (!std::isnan(seq.disp[f][ri])) ++valid;
    if (valid < cfg.min_valid_samples) {
      const size_t r = ri / static_cast<size_t>(cfg.dcc_grid_w);
      const size_t c = ri % static_cast<size_t>(cfg.dcc_grid_w);
      throw DccError(ErrorCode::E_D03,
                     "區域 (r=" + std::to_string(r) + ", c=" + std::to_string(c) +
                         ") 有效樣本 " + std::to_string(valid) + " < " +
                         std::to_string(cfg.min_valid_samples));
    }
  }
  return seq;
}

DispSeq load_file(const std::string& path, const ReaderConfig& cfg) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw DccError(ErrorCode::E_D01, "disp_seq 檔案無法開啟:" + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return load(ss.str(), cfg);
}

}  // namespace dcc::io

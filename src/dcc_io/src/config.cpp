#include "dcc_io/config.hpp"

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "dcc_core/error.hpp"

namespace dcc::io {

namespace {

using nlohmann::json;

const json& need(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end()) throw DccError(ErrorCode::E_A01, std::string("config 缺必填欄位:") + key);
  return *it;
}

// FNV-1a 64-bit(config 快照雜湊;跨平台確定性)。
std::string fnv1a_hex(const std::string& s) {
  std::uint64_t h = 1469598103934665603ULL;
  for (char ch : s) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
    h *= 1099511628211ULL;
  }
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return std::string(buf);
}

// 由 pattern period 與 offsets 推導 pitch:pitch = period / (該軸相異座標數)。
int derive_pitch(const json& offsets, int period, int axis, const char* axis_name) {
  std::set<int> uniq;
  for (const auto& p : offsets) uniq.insert(p.at(static_cast<size_t>(axis)).get<int>());
  if (uniq.empty() || period % static_cast<int>(uniq.size()) != 0)
    throw DccError(ErrorCode::E_A01,
                   std::string("pitch 推導失敗(") + axis_name + " 軸閉環驗算不符)");
  return period / static_cast<int>(uniq.size());
}

}  // namespace

AppConfig load_config(const std::string& json_text) {
  json j;
  try {
    j = json::parse(json_text);
  } catch (const json::parse_error& e) {
    throw DccError(ErrorCode::E_A01, std::string("config JSON 解析失敗:") + e.what());
  }

  AppConfig c;
  const json& sensor = need(j, "sensor");
  const json& vcm = need(j, "vcm");
  const json& dcc_s = need(j, "dcc");

  // ── sensor + pitch 推導(FR-02/鐵律 5:閉環驗算)──────────────────────
  c.sensor_width = need(sensor, "width").get<int>();
  c.sensor_height = need(sensor, "height").get<int>();
  c.pattern_period_x = need(sensor, "pattern_period_x").get<int>();
  c.pattern_period_y = need(sensor, "pattern_period_y").get<int>();
  if (c.pattern_period_x <= 0 || c.sensor_width % c.pattern_period_x != 0 ||
      c.pattern_period_y <= 0 || c.sensor_height % c.pattern_period_y != 0)
    throw DccError(ErrorCode::E_A01, "sensor 尺寸不可被 pattern period 整除");

  const json& lofs = need(sensor, "pd_left_offsets");
  const json& rofs = need(sensor, "pd_right_offsets");
  if (lofs.size() != rofs.size() || lofs.empty())
    throw DccError(ErrorCode::E_A01, "pd offsets 左右數量不一致或為空");
  for (size_t i = 0; i < lofs.size(); ++i) {
    const int lx = lofs[i].at(0).get<int>(), ly = lofs[i].at(1).get<int>();
    const int rx = rofs[i].at(0).get<int>(), ry = rofs[i].at(1).get<int>();
    if (lx < 0 || lx >= c.pattern_period_x || rx < 0 || rx >= c.pattern_period_x)
      throw DccError(ErrorCode::E_A01, "pd offset x 超出 pattern period");
    if (ly != ry)
      throw DccError(ErrorCode::E_A01, "第 " + std::to_string(i) + " 對 L/R offset 不同列(非同子格)");
  }
  c.pitch_x = derive_pitch(lofs, c.pattern_period_x, 0, "x");
  c.pitch_y = derive_pitch(lofs, c.pattern_period_y, 1, "y");

  // ── vcm ──────────────────────────────────────────────────────────────
  c.vcm.dac_min = need(vcm, "dac_min").get<int>();
  c.vcm.dac_max = need(vcm, "dac_max").get<int>();
  c.vcm.af_cal_inf = need(vcm, "af_cal_inf").get<int>();
  c.vcm.af_cal_macro = need(vcm, "af_cal_macro").get<int>();
  if (c.vcm.af_cal_inf >= c.vcm.af_cal_macro)
    throw DccError(ErrorCode::E_A01, "AF 校正值非法:INF >= MACRO(FR-02)");
  if (c.vcm.af_cal_inf < c.vcm.dac_min || c.vcm.af_cal_macro > c.vcm.dac_max)
    throw DccError(ErrorCode::E_A01, "AF 校正值超出 DAC 範圍");

  // ── dcc ──────────────────────────────────────────────────────────────
  c.sweep.far_margin = need(dcc_s, "far_margin").get<double>();
  c.sweep.near_margin = need(dcc_s, "near_margin").get<double>();
  c.sweep.num_positions = need(dcc_s, "num_positions").get<int>();
  c.grid_w = need(dcc_s, "grid_w").get<int>();
  c.grid_h = need(dcc_s, "grid_h").get<int>();
  c.q_format = need(dcc_s, "q_format").get<int>();
  c.tolerance = need(dcc_s, "tolerance").get<double>();
  c.min_valid_samples = dcc_s.value("min_valid_samples", 8);
  c.smooth_limit = dcc_s.value("smooth_limit", 0.25);
  c.r2_warn = dcc_s.value("r2_warn", 0.98);
  c.input_disparity_unit = need(dcc_s, "input_disparity_unit").get<std::string>();
  c.output_disparity_unit = need(dcc_s, "output_disparity_unit").get<std::string>();
  for (const std::string* u : {&c.input_disparity_unit, &c.output_disparity_unit})
    if (*u != "raw_pixel" && *u != "pd_image_grid")
      throw DccError(ErrorCode::E_A01, "disparity_unit 非法:" + *u);
  if (c.q_format < 4 || c.q_format > 8)
    throw DccError(ErrorCode::E_A01, "q_format 須在 4..8");
  if (c.sweep.far_margin < 0.0 || c.sweep.far_margin > 0.2 || c.sweep.near_margin < 0.0 ||
      c.sweep.near_margin > 0.2)
    throw DccError(ErrorCode::E_A01, "far/near_margin 須在 0..0.2");
  if (c.tolerance <= 0.0 || c.tolerance >= 1.0)
    throw DccError(ErrorCode::E_A01, "tolerance 須在 (0,1)");
  if (c.grid_w != 8 || c.grid_h != 6)
    throw DccError(ErrorCode::E_A01, "grid 固定 8×6(v1)");
  if (c.sweep.num_positions != 10)
    throw DccError(ErrorCode::E_A01, "num_positions 固定 10(v1)");

  // ── focus / aggregation(選填,含預設)────────────────────────────────
  if (j.contains("focus")) {
    c.focus_poly_order = j["focus"].value("poly_order", 4);
    c.peak_margin_steps = j["focus"].value("peak_margin_steps", 1);
  }
  if (j.contains("aggregation")) {
    const std::string m = j["aggregation"].value("method", "median");
    if (m == "median") c.agg_method = dcc::aggregate::Method::median;
    else if (m == "weighted_mean") c.agg_method = dcc::aggregate::Method::weighted_mean;
    else throw DccError(ErrorCode::E_A01, "aggregation.method 非法:" + m);
    c.min_valid_ratio = j["aggregation"].value("min_valid_ratio", 0.5);
  }

  c.snapshot = j.dump();  // 正規化(鍵排序)後快照
  c.hash = fnv1a_hex(c.snapshot);
  return c;
}

const char* default_config_json() {
  // 與 config/sensor_config_example.json 同值(假設模組)。
  return R"json({
    "sensor": {
      "name": "example_metal_shield", "width": 4608, "height": 3456,
      "bit_depth": 10, "black_level": 64,
      "pattern_period_x": 32, "pattern_period_y": 32,
      "pd_left_offsets":  [[4,3],[20,3],[4,11],[20,11],[4,19],[20,19],[4,27],[20,27]],
      "pd_right_offsets": [[8,3],[24,3],[8,11],[24,11],[8,19],[24,19],[8,27],[24,27]],
      "orientation": "canonical (flip/mirror off)"
    },
    "vcm": { "dac_bits": 10, "dac_min": 0, "dac_max": 1023,
             "af_cal_inf": 220, "af_cal_macro": 620, "settle_time_ms": 30 },
    "dcc": { "far_margin": 0.1, "near_margin": 0.1, "num_positions": 10,
             "grid_w": 8, "grid_h": 6, "q_format": 6, "tolerance": 0.2,
             "min_valid_samples": 8, "smooth_limit": 0.25, "r2_warn": 0.98,
             "input_disparity_unit": "raw_pixel", "output_disparity_unit": "raw_pixel" },
    "focus": { "poly_order": 4, "peak_margin_steps": 1 },
    "aggregation": { "method": "median", "min_valid_ratio": 0.5 }
  })json";
}

std::string serialize_config(const AppConfig& c) {
  json j = json::parse(c.snapshot);  // sensor 段(offsets 等)以原 snapshot 為準
  j["vcm"]["af_cal_inf"] = c.vcm.af_cal_inf;
  j["vcm"]["af_cal_macro"] = c.vcm.af_cal_macro;
  j["vcm"]["dac_min"] = c.vcm.dac_min;
  j["vcm"]["dac_max"] = c.vcm.dac_max;
  auto& d = j["dcc"];
  d["far_margin"] = c.sweep.far_margin;
  d["near_margin"] = c.sweep.near_margin;
  d["num_positions"] = c.sweep.num_positions;
  d["grid_w"] = c.grid_w;
  d["grid_h"] = c.grid_h;
  d["q_format"] = c.q_format;
  d["tolerance"] = c.tolerance;
  d["min_valid_samples"] = c.min_valid_samples;
  d["smooth_limit"] = c.smooth_limit;
  d["r2_warn"] = c.r2_warn;
  d["input_disparity_unit"] = c.input_disparity_unit;
  d["output_disparity_unit"] = c.output_disparity_unit;
  j["focus"]["poly_order"] = c.focus_poly_order;
  j["focus"]["peak_margin_steps"] = c.peak_margin_steps;
  j["aggregation"]["method"] =
      (c.agg_method == dcc::aggregate::Method::weighted_mean) ? "weighted_mean" : "median";
  j["aggregation"]["min_valid_ratio"] = c.min_valid_ratio;
  return j.dump();
}

AppConfig load_config_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw DccError(ErrorCode::E_A01, "config 檔案無法開啟:" + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return load_config(ss.str());
}

}  // namespace dcc::io

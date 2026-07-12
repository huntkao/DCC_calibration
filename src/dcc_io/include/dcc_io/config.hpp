// Config 載入與驗證(SPEC-004 §2 schema v1;FR-01/FR-02)。
// 缺欄位或非法值 → DccError(E_A01),拒絕啟動。
#pragma once

#include <string>

#include "dcc_core/aggregate.hpp"
#include "dcc_core/sweep.hpp"

namespace dcc::io {

struct AppConfig {
  // sensor(節錄;pitch 由 pattern 與 offsets 推導)
  int sensor_width = 0, sensor_height = 0;
  int pattern_period_x = 0, pattern_period_y = 0;
  int pitch_x = 0, pitch_y = 0;  // 推導值(範例模組 16 / 8)

  // vcm / sweep
  dcc::sweep::VcmParams vcm;
  dcc::sweep::SweepParams sweep;

  // dcc
  int grid_w = 8, grid_h = 6;
  int q_format = 6;
  double tolerance = 0.20;
  int min_valid_samples = 8;
  double smooth_limit = 0.25;
  double r2_warn = 0.98;
  std::string input_disparity_unit = "raw_pixel";
  std::string output_disparity_unit = "raw_pixel";

  // focus
  int focus_poly_order = 4;
  int peak_margin_steps = 1;

  // aggregation
  dcc::aggregate::Method agg_method = dcc::aggregate::Method::median;
  double min_valid_ratio = 0.5;

  // 快照與雜湊(報告/一致性用)
  std::string snapshot;  // 正規化後之 config JSON 全文
  std::string hash;      // FNV-1a 64(hex)
};

AppConfig load_config(const std::string& json_text);
AppConfig load_config_file(const std::string& path);

// 內建預設 config(與 config/sensor_config_example.json 同值;dry-run 用)。
const char* default_config_json();

}  // namespace dcc::io

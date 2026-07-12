// 離線管線(SPEC-002 Phase A→G 串接;dcc_app 層)。
// 單位:disp = raw_pixel、DCC = DAC/raw_pixel(core 單一單位區)。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dcc_core/validate.hpp"
#include "dcc_io/config.hpp"
#include "dcc_io/disp_seq_reader.hpp"

namespace dcc::app {

struct RegionResult {
  double dcc_raw_px = 0.0;   // [DAC/raw_px]
  double dcc_pd_grid = 0.0;  // [DAC/pd_grid](稽核用,SPEC-004 §3)
  double intercept = 0.0;    // [DAC]
  double r2 = 0.0;
  double focus_peak = 0.0;   // [DAC]
  double err = 0.0;          // |b − peak| / span
  bool pass = false;
  int n_valid = 0;
};

struct RunResult {
  std::string module_id;
  std::vector<int> dacs;                 // C-1 規劃(= 序列驗證後之 DAC)
  double span = 0.0;                     // NEAR − FAR [DAC]
  dcc::io::DispSeq seq;                  // 驗證/聚合後之輸入(重算/報告用)
  std::vector<RegionResult> regions;     // grid_h×grid_w row-major(48)
  bool pass = false;
  std::vector<dcc::validate::WorstRegion> worst;
  std::vector<std::string> warnings;     // r² / 平滑性(FR-15)
  std::vector<std::uint8_t> block;       // 校正 block(依 output_disparity_unit)
};

// 執行 A→G;任何 Phase 失敗以 DccError 上拋(現場落盤由 session 層負責)。
// gain_l/gain_r:NVM gain map 透傳(前期由 SimNvm/呼叫端提供;13×17)。
RunResult run(const dcc::io::AppConfig& cfg, const std::string& disp_seq_json,
              const std::vector<double>& gain_l, const std::vector<double>& gain_r,
              int gain_w = 13, int gain_h = 17);

}  // namespace dcc::app

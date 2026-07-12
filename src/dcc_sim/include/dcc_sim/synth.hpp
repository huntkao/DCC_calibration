// 合成 disparity/focus 序列生成器(SPEC-005 §2;IT/UT 之資料來源)。
// 輸出:符合 SPEC-004 §3a 之 disp_seq JSON 文字。
// 真值:disp = (dac − focus_center) / true_dcc(r,c) [raw_pixel]。
#pragma once

#include <string>
#include <tuple>
#include <vector>

namespace dcc::sim {

struct SynthSpec {
  std::string module_id = "SIM0001";
  std::vector<int> dacs;              // 必填(通常來自 sweep::plan)
  std::string unit = "raw_pixel";     // "raw_pixel" | "pd_image_grid"
  int pitch_x = 16;
  int grid_w = 8, grid_h = 6;         // 輸出粒度(可設細粒度測 D-5)
  double focus_center = 420.0;        // 合成合焦位置 [DAC]
  double center_dcc = 12.46;          // 中央真值 [DAC/raw_px]
  double corner_dcc = 14.5;           // 角落真值(線性升)
  double noise_sigma = 0.0;           // disparity 高斯雜訊 σ [raw_px]
  double bias = 0.0;                  // disparity 系統偏差 [raw_px]
  unsigned seed = 0;                  // 雜訊種子(確定性)
  std::vector<std::tuple<int, int, int>> null_cells;  // (frame, r, c) → null
  bool with_quality = false;          // 輸出 quality 面(定值 1.0)
};

// 逐 cell 真值 DCC:中央 center_dcc,向角落線性升至 corner_dcc。
double true_dcc(int r, int c, int grid_w, int grid_h, double center_dcc, double corner_dcc);

// 生成 JSON 文字(可直接餵 disp_seq_reader 或落盤)。
std::string generate(const SynthSpec& spec);

// 結構化縮排:標量鍵各一行、data/focus/quality 每「列」一行——
// 人工可讀且不逐元素爆行;輸出與輸入為等值 JSON。
std::string pretty(const std::string& compact_json);

}  // namespace dcc::sim

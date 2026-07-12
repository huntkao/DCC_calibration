// 輸出單位:disp 一律 raw_pixel(讀取端已依 input_disparity_unit 轉換,
// 全案入向轉換僅此一處,SPEC-003 §3);focus 量綱不拘。
// disparity/focus 序列讀取與驗證(SPEC-004 §3a、SPEC-002 Phase D-1..D-6)。
#pragma once

#include <string>
#include <vector>

#include "dcc_core/aggregate.hpp"

namespace dcc::io {

struct ReaderConfig {
  int pitch_x = 16;
  std::string input_disparity_unit = "raw_pixel";  // "raw_pixel" | "pd_image_grid"
  int num_positions = 10;
  int dcc_grid_w = 8;
  int dcc_grid_h = 6;
  std::vector<int> planned_dacs;  // C-1 規劃;非空時逐點比對(容差 ±1 DAC)
  dcc::aggregate::Method agg_method = dcc::aggregate::Method::median;
  double min_valid_ratio = 0.5;   // D-5
  int min_valid_samples = 8;      // D-6
};

struct DispSeq {
  std::string module_id;
  std::vector<int> dacs;                     // [N],嚴格遞增(FAR→NEAR)
  // 以下皆聚合後之 dcc.grid(row-major [grid_h][grid_w] 展平),NaN = 無效
  std::vector<std::vector<double>> disp;     // [N][h*w],raw_pixel
  std::vector<std::vector<double>> focus;    // [N][h*w]
  std::vector<std::vector<double>> quality;  // [N][h*w];輸入無 quality 時為空
  int grid_w = 0, grid_h = 0;                // == config dcc grid
};

// 由 JSON 文字載入並執行 D-1..D-6 驗證。
// 失敗:schema/形狀 → DccError(E_D01);DAC/unit/pitch 不一致 → DccError(E_D02);
//       區域有效樣本不足 → DccError(E_D03)。
DispSeq load(const std::string& json_text, const ReaderConfig& cfg);

// 檔案版包裝;讀檔失敗 → DccError(E_D01)。
DispSeq load_file(const std::string& path, const ReaderConfig& cfg);

}  // namespace dcc::io

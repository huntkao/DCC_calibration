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
  // 非線性注入:disp = (u/k)·(1 + nl2·(u/H) − nl3·(u/H)²),u = dac−fc,
  // H = sweep 半幅(由 dacs 動態推導,預設 config = 240)。
  double nonlinearity = 0.0;          // nl2:偶次「不對稱」項(球差等造成近/遠端不對稱;
                                      //      殘差 ∝ u²,兩端同向偏移,鐘型)
  double s_curve = 0.0;               // nl3:奇次「S 型壓縮」項(canonical:PD 角度響應飽和、
                                      //      模糊圈超出相關窗;殘差 ∝ −u³,兩端反向,
                                      //      正值 = 端點視差幅度壓縮 nl3×100%)
  double focus_peak_offset = 0.0;     // focus 峰值相對 disparity 合焦點之系統性偏移 [DAC]
                                      // (模擬場曲/chart 傾斜/PD vs 對比合焦分歧;
                                      //  err = |offset|/span,offset 96 → err 0.20 踩線)
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

// ── 合成 RAW(UI 底圖展示/讀取器測試用;M2 後被實機 RAW 取代)──────────
struct RawSpec {
  int width = 4608, height = 3456;
  int black_level = 64;                  // 10-bit,值域 0..1023
  int period_x = 32, period_y = 32;      // PD pattern 週期
  // 假設模組之 PD offsets(L/R 各 8 對)
  std::vector<std::pair<int, int>> pd_left = {{4, 3}, {20, 3}, {4, 11}, {20, 11},
                                              {4, 19}, {20, 19}, {4, 27}, {20, 27}};
  std::vector<std::pair<int, int>> pd_right = {{8, 3}, {24, 3}, {8, 11}, {24, 11},
                                               {8, 19}, {24, 19}, {8, 27}, {24, 27}};
  double pd_attenuation = 0.45;          // metal-shield 遮光 → PD 像素較暗
  unsigned seed = 0;
};

// 產生單張 RAW(uint16,row-major,值域 0..1023):
// 垂直線紋理(混合弦波,避免週期病態)+ 輕微 vignette + PD 像素遮光。
std::vector<std::uint16_t> generate_raw(const RawSpec& spec);

}  // namespace dcc::sim

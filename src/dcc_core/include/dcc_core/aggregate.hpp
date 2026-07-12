// 輸入/輸出單位:與輸入值同(disparity = raw_pixel、focus = 量綱不拘)。
// 粒度聚合(SPEC-002 D-5):外部 SAD 細粒度 cell → dcc.grid(8×6)。
// 歸區:cell 中心座標對等分切割歸區,落在邊界者歸左/上區(確定性)。
#pragma once

#include <vector>

namespace dcc::aggregate {

enum class Method {
  median,         // 預設,抗離群
  weighted_mean,  // 以 quality 為權重;缺 quality 則等權
};

struct GridSize {
  int w = 0;
  int h = 0;
};

// cells / quality:row-major [h][w] 展平;NaN = 無效 cell。
// quality 為 nullptr 或空 → 等權。
// 區內有效 cell 比例 < min_valid_ratio → 該區輸出 NaN(== 視為有效)。
// in == out 時為直通(D-5 跳過語意)。
// 失敗:尺寸閉環驗算不符 → DccError(E_D01)。
std::vector<double> aggregate(const std::vector<double>& cells, GridSize in, GridSize out,
                              Method method, const std::vector<double>* quality,
                              double min_valid_ratio);

}  // namespace dcc::aggregate

// UT-10(SPEC-005 §3):粒度聚合 D-5。
// 準則:無雜訊細粒度(144×108)經 median/weighted_mean 聚合,皆與直接 8×6 一致
//       (<1e-9);區內有效 cell 比例 0.49/0.50 邊界判定正確。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/aggregate.hpp"

using namespace dcc::aggregate;

namespace {
constexpr GridSize kOut{8, 6};

// 依 8×6 歸區規則回推 cell 所屬區之值:v = r_out*8 + c_out。
double region_value_of_cell(int r, int c, GridSize in) {
  const int orow = static_cast<int>((static_cast<double>(r) + 0.5) * kOut.h / in.h);
  const int ocol = static_cast<int>((static_cast<double>(c) + 0.5) * kOut.w / in.w);
  return static_cast<double>(orow * kOut.w + ocol);
}

std::vector<double> fine_cells(GridSize in) {
  std::vector<double> cells;
  for (int r = 0; r < in.h; ++r)
    for (int c = 0; c < in.w; ++c) cells.push_back(region_value_of_cell(r, c, in));
  return cells;
}
}  // namespace

TEST_CASE("UT-10: 144×108 細粒度 median/weighted 聚合皆與直接 8×6 一致", "[ut10][aggregate]") {
  const GridSize in{144, 108};  // pattern block 粒度
  const auto cells = fine_cells(in);

  const auto med = aggregate(cells, in, kOut, Method::median, nullptr, 0.5);
  const auto wm = aggregate(cells, in, kOut, Method::weighted_mean, nullptr, 0.5);
  REQUIRE(med.size() == 48);
  for (size_t i = 0; i < 48; ++i) {
    REQUIRE(std::fabs(med[i] - static_cast<double>(i)) < 1e-9);
    REQUIRE(std::fabs(wm[i] - static_cast<double>(i)) < 1e-9);
  }
}

TEST_CASE("UT-10: 有效比例邊界 0.49 → NaN、0.50 → 有效", "[ut10][aggregate]") {
  const GridSize in{80, 60};  // 每區 10×10 = 100 cells
  std::vector<double> cells(80 * 60, 5.0);

  // 區 (0,0) = rows 0..9 × cols 0..9;將前 50 cells 設 NaN → 有效 50/100 = 0.50。
  int killed = 0;
  for (int r = 0; r < 10 && killed < 50; ++r)
    for (int c = 0; c < 10 && killed < 50; ++c) {
      cells[static_cast<size_t>(r) * 80 + static_cast<size_t>(c)] = std::nan("");
      ++killed;
    }
  auto out = aggregate(cells, in, kOut, Method::median, nullptr, 0.5);
  REQUIRE_FALSE(std::isnan(out[0]));  // 0.50 == min_valid_ratio → 有效
  REQUIRE(std::fabs(out[0] - 5.0) < 1e-12);

  // 再殺一個 → 有效 49/100 = 0.49 < 0.5 → NaN;其他區不受影響。
  cells[static_cast<size_t>(5) * 80 + 0] = std::nan("");
  out = aggregate(cells, in, kOut, Method::median, nullptr, 0.5);
  REQUIRE(std::isnan(out[0]));
  REQUIRE(std::fabs(out[1] - 5.0) < 1e-12);
}

TEST_CASE("UT-10: weighted_mean 以 quality 為權重(權重 0 之離群值被排除)", "[ut10][aggregate]") {
  const GridSize in{16, 12};  // 每區 2×2 = 4 cells
  std::vector<double> cells(16 * 12, 1.0), quality(16 * 12, 1.0);
  // 區 (0,0) 注入離群值 9.0,quality = 0 → weighted_mean 應為 1.0。
  cells[0] = 9.0;
  quality[0] = 0.0;
  const auto wm = aggregate(cells, in, kOut, Method::weighted_mean, &quality, 0.5);
  REQUIRE(std::fabs(wm[0] - 1.0) < 1e-12);
  // median 同樣抗離群:{9,1,1,1} 之中位數 = 1。
  const auto med = aggregate(cells, in, kOut, Method::median, nullptr, 0.5);
  REQUIRE(std::fabs(med[0] - 1.0) < 1e-12);
}

TEST_CASE("UT-10: 粒度一致(8×6 → 8×6)為直通,含 NaN 原樣保留", "[ut10][aggregate]") {
  std::vector<double> cells(48, 2.5);
  cells[7] = std::nan("");
  const auto out = aggregate(cells, {8, 6}, kOut, Method::median, nullptr, 0.5);
  REQUIRE(out.size() == 48);
  REQUIRE(std::isnan(out[7]));
  REQUIRE(std::fabs(out[0] - 2.5) < 1e-12);
}

// UT-03(SPEC-005 §3):單位轉換 FR-09。
// 準則:同一真值之 pd_image_grid 序列 ×16 後與 raw_pixel 序列結果 bit-exact 一致。
#include <catch2/catch_test_macros.hpp>

#include "dcc_io/disp_seq_reader.hpp"
#include "test_helpers.hpp"

using dcc::io::load;
using namespace testutil;

TEST_CASE("UT-03: pd_image_grid 輸入 ×pitch_x 後與 raw_pixel 輸入 bit-exact 一致",
          "[ut03][reader][units]") {
  // 基準值以 pd_image_grid 表述;raw 版由測試端以同一乘法生成,
  // 讀取端對 pd 版做同一乘法 → 兩者必須逐 bit 相等。
  const auto pd_value = [](size_t f, int, int) {
    return truth_disp_raw_px(default_dacs()[f]) / 16.0;
  };

  SeqSpec raw_spec;
  raw_spec.unit = "raw_pixel";
  raw_spec.disp = [&](size_t f, int r, int c) { return pd_value(f, r, c) * 16.0; };

  SeqSpec pd_spec;
  pd_spec.unit = "pd_image_grid";
  pd_spec.disp = pd_value;

  auto cfg_raw = default_reader_cfg();
  cfg_raw.input_disparity_unit = "raw_pixel";
  auto cfg_pd = default_reader_cfg();
  cfg_pd.input_disparity_unit = "pd_image_grid";

  const auto seq_raw = load(make_seq_json(raw_spec).dump(), cfg_raw);
  const auto seq_pd = load(make_seq_json(pd_spec).dump(), cfg_pd);

  REQUIRE(seq_raw.disp.size() == seq_pd.disp.size());
  for (size_t f = 0; f < seq_raw.disp.size(); ++f)
    for (size_t i = 0; i < seq_raw.disp[f].size(); ++i)
      REQUIRE(seq_raw.disp[f][i] == seq_pd.disp[f][i]);  // bit-exact,不用容差
}

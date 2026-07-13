// Config 載入/驗證(FR-01/02,E-A01)與序列化一致性閉環(M1d)。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <nlohmann/json.hpp>

#include "dcc_core/error.hpp"
#include "dcc_io/config.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::io::load_config;
using dcc::io::serialize_config;

namespace {
template <typename Mutator>
void expect_a01(Mutator&& mutate) {
  auto j = nlohmann::json::parse(dcc::io::default_config_json());
  mutate(j);
  try {
    load_config(j.dump());
    FAIL("應拋出 DccError(E-A01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_A01);
  }
}
}  // namespace

TEST_CASE("config:預設載入成功,pitch 由 offsets 閉環推導(16/8)", "[config]") {
  const auto c = load_config(dcc::io::default_config_json());
  REQUIRE(c.pitch_x == 16);
  REQUIRE(c.pitch_y == 8);
  REQUIRE(c.sweep.far_margin == 0.1);
  REQUIRE_FALSE(c.hash.empty());
}

TEST_CASE("config:缺必填欄位 / 非法值 → E-A01(FR-01/02)", "[config][error]") {
  expect_a01([](nlohmann::json& j) { j["dcc"].erase("tolerance"); });
  expect_a01([](nlohmann::json& j) { j["vcm"]["af_cal_inf"] = 700; });   // INF >= MACRO
  expect_a01([](nlohmann::json& j) { j["dcc"]["q_format"] = 12; });      // 超出 4..8
  expect_a01([](nlohmann::json& j) { j["dcc"]["far_margin"] = 0.5; });   // 超出 0..0.2
  expect_a01([](nlohmann::json& j) { j["sensor"]["width"] = 4600; });    // 不可被 period 整除
  expect_a01([](nlohmann::json& j) {
    j["dcc"]["input_disparity_unit"] = "pixels";                          // 非法 enum
  });
}

TEST_CASE("config:num_positions 交叉約束(與 min_valid_samples/poly_order/margin_steps 連動)",
          "[config][error]") {
  // 樣本門檻 > 掃描點數 → 必然全區 E-D03,載入即拒絕。
  expect_a01([](nlohmann::json& j) { j["dcc"]["num_positions"] = 6; });  // mvs 預設 8 > 6
  // 擬合階數 >= 點數 → 無法擬合。
  expect_a01([](nlohmann::json& j) {
    j["dcc"]["num_positions"] = 5;
    j["dcc"]["min_valid_samples"] = 4;
    j["focus"]["poly_order"] = 5;
  });
  // E-F01 有效範圍為空(2×steps >= N−1)。
  expect_a01([](nlohmann::json& j) {
    j["dcc"]["num_positions"] = 5;
    j["dcc"]["min_valid_samples"] = 4;
    j["focus"]["poly_order"] = 3;
    j["focus"]["peak_margin_steps"] = 2;
  });
  // 範圍外。
  expect_a01([](nlohmann::json& j) { j["dcc"]["num_positions"] = 4; });
  expect_a01([](nlohmann::json& j) { j["dcc"]["num_positions"] = 60; });
}

TEST_CASE("config:serialize → load 閉環(UI 編輯值經同一驗證器,hash 更新)",
          "[config][roundtrip]") {
  auto c = load_config(dcc::io::default_config_json());
  const std::string hash0 = c.hash;

  // 模擬 UI 編輯。
  c.tolerance = 0.15;
  c.sweep.far_margin = 0.05;
  c.output_disparity_unit = "pd_image_grid";
  c.agg_method = dcc::aggregate::Method::weighted_mean;

  const auto c2 = load_config(serialize_config(c));
  REQUIRE(c2.tolerance == 0.15);
  REQUIRE(c2.sweep.far_margin == 0.05);
  REQUIRE(c2.output_disparity_unit == "pd_image_grid");
  REQUIRE(c2.agg_method == dcc::aggregate::Method::weighted_mean);
  REQUIRE(c2.pitch_x == 16);          // sensor 段保留
  REQUIRE(c2.hash != hash0);          // hash 隨值更新

  // 非法編輯必須被同一驗證器攔下。
  c.q_format = 12;
  try {
    load_config(serialize_config(c));
    FAIL("應拋出 DccError(E-A01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_A01);
  }
}

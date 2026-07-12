// UT-02(SPEC-005 §3):序列讀取 FR-08。
// 準則:合法序列載入成功;形狀錯 → E-D01;DAC 不符/非遞增/unit 錯 → E-D02。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/error.hpp"
#include "dcc_io/disp_seq_reader.hpp"
#include "test_helpers.hpp"

using dcc::DccError;
using dcc::ErrorCode;
using dcc::io::load;
using namespace testutil;

namespace {
template <typename Mutator>
void expect_error(ErrorCode code, Mutator&& mutate,
                  dcc::io::ReaderConfig cfg = default_reader_cfg()) {
  SeqSpec spec;
  auto j = make_seq_json(spec);
  mutate(j);
  try {
    load(j.dump(), cfg);
    FAIL("應拋出 DccError");
  } catch (const DccError& e) {
    REQUIRE(e.code() == code);
  }
}
}  // namespace

TEST_CASE("UT-02: 合法序列載入成功,內容正確", "[ut02][reader]") {
  const auto seq = load(make_seq_json(SeqSpec{}).dump(), default_reader_cfg());
  REQUIRE(seq.module_id == "TEST0001");
  REQUIRE(seq.dacs == default_dacs());
  REQUIRE(seq.disp.size() == 10);
  REQUIRE(seq.disp[0].size() == 48);
  REQUIRE(seq.grid_w == 8);
  REQUIRE(seq.grid_h == 6);
  REQUIRE(seq.quality.empty());  // 選填欄位未提供
  // 首幀 disparity == 真值
  REQUIRE(std::fabs(seq.disp[0][0] - truth_disp_raw_px(default_dacs()[0])) < 1e-12);
  REQUIRE(std::fabs(seq.focus[3][10] - truth_focus(default_dacs()[3])) < 1e-12);
}

TEST_CASE("UT-02: 幀數 != num_positions → E-D01", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D01, [](nlohmann::json& j) { j["data"].erase(9); });
}

TEST_CASE("UT-02: 行數與 grid_w 不符 → E-D01", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D01, [](nlohmann::json& j) { j["data"][0][0].erase(7); });
}

TEST_CASE("UT-02: 缺必填欄位(focus)→ E-D01", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D01, [](nlohmann::json& j) { j.erase("focus"); });
}

TEST_CASE("UT-02: dacs 非嚴格遞增 → E-D02", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D02, [](nlohmann::json& j) {
    auto d = j["dacs"].get<std::vector<int>>();
    std::swap(d[3], d[4]);
    j["dacs"] = d;
  });
}

TEST_CASE("UT-02: dacs 與 C-1 規劃差 > 1 DAC → E-D02", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D02, [](nlohmann::json& j) {
    auto d = j["dacs"].get<std::vector<int>>();
    d[5] += 3;
    j["dacs"] = d;
  });
}

TEST_CASE("UT-02: unit 與 config 不一致 → E-D02", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D02,
               [](nlohmann::json& j) { j["unit"] = "pd_image_grid"; });  // cfg 為 raw_pixel
}

TEST_CASE("UT-02: pitch_x 與 config 不一致 → E-D02", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D02, [](nlohmann::json& j) { j["pitch_x"] = 8; });
}

TEST_CASE("UT-02: 非法 unit 字串 → E-D02", "[ut02][reader][error]") {
  expect_error(ErrorCode::E_D02, [](nlohmann::json& j) { j["unit"] = "pixels"; });
}

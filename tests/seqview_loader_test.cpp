// dcc_seqview 獨立工具之核心邏輯測試(零依賴 dcc_core/dcc_io,見 seq_loader.hpp 註解)。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <nlohmann/json.hpp>

#include "dcc_seqview/seq_loader.hpp"

using nlohmann::json;
using dcc::seqview::load;
using dcc::seqview::Severity;

namespace {

// 2×2 grid、10 幀之最小合法序列(可選 quality)。
json valid_seq(bool with_quality = false) {
  json j;
  j["module_id"] = "EXT_TEAM_01";
  j["unit"] = "raw_pixel";
  j["pitch_x"] = 16;
  j["dacs"] = {180, 233, 287, 340, 393, 447, 500, 553, 607, 660};
  j["grid_w"] = 2;
  j["grid_h"] = 2;
  j["data"] = json::array();
  j["focus"] = json::array();
  if (with_quality) j["quality"] = json::array();
  for (int f = 0; f < 10; ++f) {
    j["data"].push_back(json{{1.0, 1.1}, {1.2, 1.3}});
    j["focus"].push_back(json{{100.0, 101.0}, {102.0, 103.0}});
    if (with_quality) j["quality"].push_back(json{{0.9, 0.8}, {0.7, 0.6}});
  }
  return j;
}

bool has_issue_containing(const dcc::seqview::LoadResult& r, Severity sev, const char* needle) {
  for (const auto& i : r.issues)
    if (i.severity == sev && i.message.find(needle) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST_CASE("seqview::load:合法序列(含 quality)→ ok 且無 issue", "[seqview]") {
  const auto r = load(valid_seq(true).dump());
  REQUIRE(r.ok);
  REQUIRE(r.issues.empty());
  REQUIRE(r.seq.module_id == "EXT_TEAM_01");
  REQUIRE(r.seq.grid_w == 2);
  REQUIRE(r.seq.grid_h == 2);
  REQUIRE(r.seq.dacs.size() == 10);
  REQUIRE(r.seq.has_quality);
  REQUIRE(r.seq.data.size() == 10);
  REQUIRE(r.seq.data[0].size() == 4);
}

TEST_CASE("seqview::load:無 quality 欄位 → has_quality=false 且無相關 issue", "[seqview]") {
  const auto r = load(valid_seq(false).dump());
  REQUIRE(r.ok);
  REQUIRE_FALSE(r.seq.has_quality);
  REQUIRE(r.seq.quality.empty());
}

TEST_CASE("seqview::load:JSON 語法錯誤 → ok=false 且有 error issue", "[seqview]") {
  const auto r = load("{not json");
  REQUIRE_FALSE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::error, "解析"));
}

TEST_CASE("seqview::load:缺 dacs → ok=false", "[seqview]") {
  auto j = valid_seq();
  j.erase("dacs");
  const auto r = load(j.dump());
  REQUIRE_FALSE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::error, "dacs"));
}

TEST_CASE("seqview::load:grid_w 非法(0)→ ok=false", "[seqview]") {
  auto j = valid_seq();
  j["grid_w"] = 0;
  const auto r = load(j.dump());
  REQUIRE_FALSE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::error, "grid"));
}

TEST_CASE("seqview::load:dacs 非嚴格遞增 → 仍 ok 但記 error issue", "[seqview]") {
  auto j = valid_seq();
  j["dacs"] = {180, 180, 287, 340, 393, 447, 500, 553, 607, 660};
  const auto r = load(j.dump());
  REQUIRE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::error, "遞增"));
}

TEST_CASE("seqview::load:data 幀數與 dacs 不符 → 記 issue 且截斷至最小共同幀數", "[seqview]") {
  auto j = valid_seq();
  j["data"].erase(9);
  j["data"].erase(8);  // data 只剩 8 幀,dacs 仍 10 幀
  const auto r = load(j.dump());
  REQUIRE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::error, "幀數"));
  REQUIRE(r.seq.dacs.size() == 8);
  REQUIRE(r.seq.data.size() == 8);
}

TEST_CASE("seqview::load:某幀形狀與 grid 不符 → 該幀以 NaN 面代替並記 issue", "[seqview]") {
  auto j = valid_seq();
  j["data"][3] = json{{1.0, 1.1}};  // 第 3 幀只有 1 列,grid_h=2 應為 2 列
  const auto r = load(j.dump());
  REQUIRE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::error, "3"));
  REQUIRE(std::isnan(r.seq.data[3][0]));
  REQUIRE(std::isnan(r.seq.data[3][3]));
  REQUIRE_FALSE(std::isnan(r.seq.data[0][0]));  // 其餘幀不受影響
}

TEST_CASE("seqview::load:quality 值域外 → warn issue", "[seqview]") {
  auto j = valid_seq(true);
  j["quality"][0] = json{{1.5, 0.8}, {0.7, 0.6}};
  const auto r = load(j.dump());
  REQUIRE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::warn, "quality"));
}

TEST_CASE("seqview::load:某 cell 全幀皆 null → warn issue 提及座標", "[seqview]") {
  auto j = valid_seq();
  for (auto& frame : j["data"]) frame[0][0] = nullptr;  // (r=0,c=0) 全幀 null
  const auto r = load(j.dump());
  REQUIRE(r.ok);
  REQUIRE(has_issue_containing(r, Severity::warn, "null"));
}

TEST_CASE("seqview::load_file:檔案不存在 → ok=false", "[seqview]") {
  const auto r = dcc::seqview::load_file("/no/such/path/disp_seq.json");
  REQUIRE_FALSE(r.ok);
  REQUIRE_FALSE(r.issues.empty());
}

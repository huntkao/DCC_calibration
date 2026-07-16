// UT:校正 block 可讀等價輸出(dcc_io/eeprom_equiv;SPEC-004 §4a)。
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "dcc_core/eeprom_codec.hpp"
#include "dcc_io/eeprom_equiv.hpp"

namespace {
const std::vector<double> kGain(221, 1.0);
std::vector<double> test_dcc() {
  std::vector<double> d(48, 12.0);
  d[2 * 8 + 3] = 12.46;  // 假設模組基準:12.46 → Q6 = 797 = 0x031D
  return d;
}
const dcc::io::BlockEquivMeta kMeta{"SIM0001", "cfg#test", "DAC/raw_pixel"};
}  // namespace

TEST_CASE("equiv: block.json 欄位與 pack 輸入一致、hex 與 encode_q 一致、checksum/長度閉環",
          "[eeprom_equiv]") {
  const auto dcc = test_dcc();
  const auto s = dcc::io::build_block_json(kMeta, kGain, kGain, 13, 17, dcc, 8, 6, 6);
  const auto j = nlohmann::json::parse(s);

  REQUIRE(j["block_version"] == dcc::eeprom::kBlockVersion);
  REQUIRE(j["module_id"] == "SIM0001");
  REQUIRE(j["dcc"]["unit"] == "DAC/raw_pixel");
  REQUIRE(j["dcc"]["q_format"] == 6);
  REQUIRE(j["dcc"]["w"] == 8);
  REQUIRE(j["dcc"]["h"] == 6);
  REQUIRE(j["dcc"]["values"][2][3] == 12.46);
  REQUIRE(j["dcc"]["encoded_hex"][2][3] == "0x031D");
  REQUIRE(j["gain"]["left"].size() == 221);
  REQUIRE(j["gain"]["right"][0] == 1.0);

  // 等價性:checksum 與 total_bytes 對 pack() 輸出閉環(驗算文化)
  const auto blob = dcc::eeprom::pack(kGain, kGain, 13, 17, dcc, 8, 6, 6);
  REQUIRE(j["total_bytes"].get<size_t>() == blob.size());
  REQUIRE(j["checksum"]["value"].get<int>() == static_cast<int>(blob.back()));
  size_t sum = 0;
  for (const auto& f : j["layout"]) sum += f["bytes"].get<size_t>();
  REQUIRE(sum == blob.size());
  REQUIRE(j["layout"][0]["offset"] == "0x0000");
}

TEST_CASE("equiv: block.txt 含物理值、hex、單位與 checksum", "[eeprom_equiv]") {
  const auto txt = dcc::io::build_block_txt(kMeta, kGain, kGain, 13, 17, test_dcc(), 8, 6, 6);
  REQUIRE(txt.find("0x031D") != std::string::npos);
  REQUIRE(txt.find("12.46") != std::string::npos);
  REQUIRE(txt.find("DAC/raw_pixel") != std::string::npos);
  REQUIRE(txt.find("checksum") != std::string::npos);
  REQUIRE(txt.find("SIM0001") != std::string::npos);
}

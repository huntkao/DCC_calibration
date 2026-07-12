// UT-08(SPEC-005 §3):eeprom FR-16。
// 準則:12.46→0x031D;checksum 破壞可偵測;pack→read round-trip。
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "dcc_core/eeprom_codec.hpp"
#include "dcc_core/error.hpp"

using dcc::DccError;
using dcc::ErrorCode;
namespace ee = dcc::eeprom;

namespace {
// 假設模組:gain 13×17=221、DCC 8×6=48、Q6。
ee::Unpacked make_and_unpack(std::vector<std::uint8_t>* out_blob = nullptr) {
  std::vector<double> gain_l(221, 1.0), gain_r(221, 1.25);
  std::vector<double> dcc(48, 12.46);
  auto blob = ee::pack(gain_l, gain_r, 13, 17, dcc, 8, 6, 6);
  if (out_blob) *out_blob = blob;
  return ee::unpack(blob);
}
}  // namespace

TEST_CASE("UT-08: Q6 編碼 12.46 → 797 = 0x031D", "[ut08][eeprom]") {
  REQUIRE(ee::encode_q(12.46, 6) == 0x031D);
  REQUIRE(std::fabs(ee::decode_q(0x031D, 6) - 12.453125) < 1e-12);  // 797/64
}

TEST_CASE("UT-08: block 總長 993、DCC 段位於 0x0380 且值為 0x031D", "[ut08][eeprom]") {
  std::vector<std::uint8_t> blob;
  make_and_unpack(&blob);
  REQUIRE(blob.size() == 993);           // 閉環:2+4+442+442+2+4+96+1
  REQUIRE(blob[0x0380] == 0x03);         // SPEC-004 §4 offset 驗算
  REQUIRE(blob[0x0381] == 0x1D);
  REQUIRE(ee::verify(blob));
}

TEST_CASE("UT-08: checksum 破壞可偵測", "[ut08][eeprom][error]") {
  std::vector<std::uint8_t> blob;
  make_and_unpack(&blob);
  blob[0x0380] ^= 0xFF;
  REQUIRE_FALSE(ee::verify(blob));
}

TEST_CASE("UT-08: pack → unpack round-trip(量化等值)", "[ut08][eeprom]") {
  const auto u = make_and_unpack();
  REQUIRE(u.version == 4);
  REQUIRE(u.gain_w == 13);
  REQUIRE(u.gain_h == 17);
  REQUIRE(u.q_format == 6);
  REQUIRE(u.dcc_w == 8);
  REQUIRE(u.dcc_h == 6);
  REQUIRE(u.gain_l.size() == 221);
  REQUIRE(u.dcc.size() == 48);
  for (double g : u.gain_l) REQUIRE(std::fabs(g - 1.0) < 1e-12);        // Q10 可精確表示
  for (double d : u.dcc) REQUIRE(std::fabs(d - 12.453125) < 1e-12);     // Q6 量化後真值
}

TEST_CASE("UT-08: Q 溢位與負值 → E-G01", "[ut08][eeprom][error]") {
  try {
    ee::encode_q(1e6, 6);
    FAIL("應拋出 DccError(E-G01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_G01);
  }
  try {
    ee::encode_q(-0.5, 6);  // DCC 無號正值(SPEC-004 §3)
    FAIL("應拋出 DccError(E-G01)");
  } catch (const DccError& e) {
    REQUIRE(e.code() == ErrorCode::E_G01);
  }
}

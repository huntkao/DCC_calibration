#include "dcc_core/eeprom_codec.hpp"

#include <cmath>
#include <string>

#include "dcc_core/error.hpp"

namespace dcc::eeprom {

namespace {

constexpr int kGainQ = 10;  // gain 固定 Q10(SPEC-004 §4)

void put_u16be(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v >> 8));
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint16_t get_u16be(const std::vector<std::uint8_t>& in, size_t off) {
  return static_cast<std::uint16_t>((in[off] << 8) | in[off + 1]);
}

std::uint8_t checksum_of(const std::vector<std::uint8_t>& blob, size_t len) {
  unsigned sum = 0;
  for (size_t i = 0; i < len; ++i) sum += blob[i];
  return static_cast<std::uint8_t>(sum % 256u);
}

}  // namespace

std::uint16_t encode_q(double value, int q) {
  if (value < 0.0)
    throw DccError(ErrorCode::E_G01, "Q 編碼值為負(DCC 應為無號正值):" + std::to_string(value));
  const double scaled = std::round(value * static_cast<double>(1 << q));
  if (scaled > 65535.0)
    throw DccError(ErrorCode::E_G01, "Q" + std::to_string(q) + " 編碼溢位:" + std::to_string(value));
  return static_cast<std::uint16_t>(scaled);
}

double decode_q(std::uint16_t raw, int q) {
  return static_cast<double>(raw) / static_cast<double>(1 << q);
}

std::vector<std::uint8_t> pack(const std::vector<double>& gain_l,
                               const std::vector<double>& gain_r, int gain_w, int gain_h,
                               const std::vector<double>& dcc, int dcc_w, int dcc_h,
                               int q_format) {
  const size_t gain_n = static_cast<size_t>(gain_w) * static_cast<size_t>(gain_h);
  const size_t dcc_n = static_cast<size_t>(dcc_w) * static_cast<size_t>(dcc_h);
  if (gain_l.size() != gain_n || gain_r.size() != gain_n)
    throw DccError(ErrorCode::E_G01, "gain map 長度與 W×H 不符(閉環驗算失敗)");
  if (dcc.size() != dcc_n)
    throw DccError(ErrorCode::E_G01, "DCC map 長度與 W×H 不符(閉環驗算失敗)");

  std::vector<std::uint8_t> blob;
  blob.reserve(2 + 4 + gain_n * 4 + 2 + 4 + dcc_n * 2 + 1);

  put_u16be(blob, kBlockVersion);
  put_u16be(blob, static_cast<std::uint16_t>(gain_w));
  put_u16be(blob, static_cast<std::uint16_t>(gain_h));
  for (double g : gain_l) put_u16be(blob, encode_q(g, kGainQ));
  for (double g : gain_r) put_u16be(blob, encode_q(g, kGainQ));
  put_u16be(blob, static_cast<std::uint16_t>(q_format));
  put_u16be(blob, static_cast<std::uint16_t>(dcc_w));
  put_u16be(blob, static_cast<std::uint16_t>(dcc_h));
  for (double d : dcc) put_u16be(blob, encode_q(d, q_format));

  blob.push_back(checksum_of(blob, blob.size()));
  return blob;
}

bool verify(const std::vector<std::uint8_t>& blob) {
  if (blob.size() < 3) return false;
  if (get_u16be(blob, 0) != kBlockVersion) return false;
  return blob.back() == checksum_of(blob, blob.size() - 1);
}

Unpacked unpack(const std::vector<std::uint8_t>& blob) {
  if (!verify(blob)) throw DccError(ErrorCode::E_G01, "block 驗證失敗(version/checksum)");

  Unpacked u;
  size_t off = 0;
  u.version = get_u16be(blob, off); off += 2;
  u.gain_w = get_u16be(blob, off); off += 2;
  u.gain_h = get_u16be(blob, off); off += 2;
  const size_t gain_n = static_cast<size_t>(u.gain_w) * static_cast<size_t>(u.gain_h);

  const size_t expect = 6 + gain_n * 4 + 6 + 0 + 1;  // 至 dcc 表頭為止 + checksum
  if (blob.size() < expect) throw DccError(ErrorCode::E_G01, "block 長度不足(gain 段)");

  u.gain_l.reserve(gain_n);
  for (size_t i = 0; i < gain_n; ++i, off += 2) u.gain_l.push_back(decode_q(get_u16be(blob, off), kGainQ));
  u.gain_r.reserve(gain_n);
  for (size_t i = 0; i < gain_n; ++i, off += 2) u.gain_r.push_back(decode_q(get_u16be(blob, off), kGainQ));

  u.q_format = get_u16be(blob, off); off += 2;
  u.dcc_w = get_u16be(blob, off); off += 2;
  u.dcc_h = get_u16be(blob, off); off += 2;
  const size_t dcc_n = static_cast<size_t>(u.dcc_w) * static_cast<size_t>(u.dcc_h);

  if (blob.size() != off + dcc_n * 2 + 1)
    throw DccError(ErrorCode::E_G01, "block 總長與表頭尺寸閉環驗算失敗");

  u.dcc.reserve(dcc_n);
  for (size_t i = 0; i < dcc_n; ++i, off += 2) u.dcc.push_back(decode_q(get_u16be(blob, off), u.q_format));
  return u;
}

}  // namespace dcc::eeprom

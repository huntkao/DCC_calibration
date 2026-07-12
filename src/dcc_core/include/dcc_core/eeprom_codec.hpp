// 校正 block 編解碼(SPEC-004 §4,開發版 v4 layout;純編碼,無檔案 I/O)。
// 輸入單位:gain 為線性增益(Q10 編碼)、dcc 依打包端已轉換之輸出單位(Q(q) 編碼)。
#pragma once

#include <cstdint>
#include <vector>

namespace dcc::eeprom {

inline constexpr std::uint16_t kBlockVersion = 4;

// 定點編碼:value × 2^q,四捨五入為 uint16。
// 失敗:value < 0 或編碼後 > 65535 → DccError(E_G01)。
std::uint16_t encode_q(double value, int q);
double decode_q(std::uint16_t raw, int q);

// 依 SPEC-004 §4 打包(BE 編碼,尾端 1 byte checksum = Σ前段 % 256)。
// 前置:gain_l/gain_r 長度 == gain_w×gain_h;dcc 長度 == dcc_w×dcc_h。
std::vector<std::uint8_t> pack(const std::vector<double>& gain_l,
                               const std::vector<double>& gain_r, int gain_w, int gain_h,
                               const std::vector<double>& dcc, int dcc_w, int dcc_h,
                               int q_format);

// checksum 與 version 驗證(回讀驗證用)。
bool verify(const std::vector<std::uint8_t>& blob);

struct Unpacked {
  std::uint16_t version = 0;
  int gain_w = 0, gain_h = 0;
  int q_format = 0;
  int dcc_w = 0, dcc_h = 0;
  std::vector<double> gain_l, gain_r, dcc;
};

// 解包(round-trip 驗證用);格式不符 → DccError(E_G01)。
Unpacked unpack(const std::vector<std::uint8_t>& blob);

}  // namespace dcc::eeprom

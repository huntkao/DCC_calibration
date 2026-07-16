// 校正 block 可讀等價輸出(SPEC-004 §4a):block.json / block.txt 字串建構,無檔案 I/O。
// 輸入單位:dcc 已依 output_disparity_unit 轉出(與 eeprom::pack 相同輸入);gain 為線性增益。
#pragma once

#include <string>
#include <vector>

namespace dcc::io {

struct BlockEquivMeta {
  std::string module_id;
  std::string config_hash;
  std::string dcc_unit;  // "DAC/raw_pixel" | "DAC/pd_image_grid"
};

// 回傳 JSON 字串(縮排 2;確定性輸出、無時間戳——與 report.json 同準則)。
// 前置與 eeprom::pack 相同:gain 長度 == gain_w×gain_h、dcc 長度 == dcc_w×dcc_h。
std::string build_block_json(const BlockEquivMeta& meta, const std::vector<double>& gain_l,
                             const std::vector<double>& gain_r, int gain_w, int gain_h,
                             const std::vector<double>& dcc, int dcc_w, int dcc_h, int q_format);

// 回傳人閱讀版(繁中;DCC 物理值與 hex 兩張 8×6 表)。
std::string build_block_txt(const BlockEquivMeta& meta, const std::vector<double>& gain_l,
                            const std::vector<double>& gain_r, int gain_w, int gain_h,
                            const std::vector<double>& dcc, int dcc_w, int dcc_h, int q_format);

}  // namespace dcc::io

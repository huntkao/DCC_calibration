#include "dcc_io/eeprom_equiv.hpp"

#include <cstdio>

#include <nlohmann/json.hpp>

#include "dcc_core/eeprom_codec.hpp"

namespace dcc::io {

namespace {

using nlohmann::json;

std::string hex16(std::uint16_t v) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "0x%04X", v);
  return buf;
}

std::string hex_off(size_t off) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "0x%04zX", off);
  return buf;
}

struct Field { std::string name; size_t bytes; std::string encoding; };

// 開發版 v4 layout(SPEC-004 §4);offset 由尺寸累加,泛用於任意 grid。
std::vector<Field> layout_fields(size_t n_gain, size_t n_dcc, int q_format) {
  return {{"version", 2, "uint16 BE"},
          {"gain_wh", 4, "uint16 BE ×2"},
          {"gain_left", 2 * n_gain, "Q10 uint16 BE"},
          {"gain_right", 2 * n_gain, "Q10 uint16 BE"},
          {"dcc_q_format", 2, "uint16 BE"},
          {"dcc_wh", 4, "uint16 BE ×2"},
          {"dcc", 2 * n_dcc, "Q" + std::to_string(q_format) + " uint16 BE"},
          {"checksum", 1, "uint8 = Σ前段 % 256"}};
}

}  // namespace

std::string build_block_json(const BlockEquivMeta& meta, const std::vector<double>& gain_l,
                             const std::vector<double>& gain_r, int gain_w, int gain_h,
                             const std::vector<double>& dcc, int dcc_w, int dcc_h, int q_format) {
  // checksum / 總長由 pack() 取得(單一真相來源,保證與 block.bin 等價)。
  const auto blob = dcc::eeprom::pack(gain_l, gain_r, gain_w, gain_h, dcc, dcc_w, dcc_h, q_format);

  json j;
  j["format"] = "dcc-block-equiv v1";
  j["block_version"] = dcc::eeprom::kBlockVersion;
  j["module_id"] = meta.module_id;
  j["config_hash"] = meta.config_hash;

  j["layout"] = json::array();
  size_t off = 0;
  for (const auto& f : layout_fields(gain_l.size(), dcc.size(), q_format)) {
    j["layout"].push_back(
        {{"field", f.name}, {"offset", hex_off(off)}, {"bytes", f.bytes}, {"encoding", f.encoding}});
    off += f.bytes;
  }
  j["total_bytes"] = blob.size();

  j["gain"] = {{"w", gain_w}, {"h", gain_h}, {"encoding", "Q10 uint16 BE"},
               {"left", gain_l}, {"right", gain_r}};

  json vals = json::array(), hexs = json::array();
  for (int r = 0; r < dcc_h; ++r) {
    json vrow = json::array(), hrow = json::array();
    for (int c = 0; c < dcc_w; ++c) {
      const double v = dcc[static_cast<size_t>(r) * static_cast<size_t>(dcc_w) +
                           static_cast<size_t>(c)];
      vrow.push_back(v);
      hrow.push_back(hex16(dcc::eeprom::encode_q(v, q_format)));
    }
    vals.push_back(vrow);
    hexs.push_back(hrow);
  }
  j["dcc"] = {{"w", dcc_w}, {"h", dcc_h}, {"unit", meta.dcc_unit}, {"q_format", q_format},
              {"values", vals}, {"encoded_hex", hexs}};

  j["checksum"] = {{"value", static_cast<int>(blob.back())}, {"hex", hex_off(blob.back())},
                   {"rule", "Σ前段 % 256"}};
  return j.dump(2);
}

std::string build_block_txt(const BlockEquivMeta& meta, const std::vector<double>& gain_l,
                            const std::vector<double>& gain_r, int gain_w, int gain_h,
                            const std::vector<double>& dcc, int dcc_w, int dcc_h, int q_format) {
  const auto blob = dcc::eeprom::pack(gain_l, gain_r, gain_w, gain_h, dcc, dcc_w, dcc_h, q_format);
  std::string t;
  char buf[128];

  t += "校正 block 可讀等價(dcc-block-equiv v1;與 block.bin 同源輸出)\n";
  t += "module_id: " + meta.module_id + "\nconfig_hash: " + meta.config_hash + "\n";
  std::snprintf(buf, sizeof(buf), "block_version: %u,total_bytes: %zu\n\n",
                dcc::eeprom::kBlockVersion, blob.size());
  t += buf;

  t += "── layout(SPEC-004 §4)──\n";
  size_t off = 0;
  for (const auto& f : layout_fields(gain_l.size(), dcc.size(), q_format)) {
    std::snprintf(buf, sizeof(buf), "%s  %5zu bytes  %-14s %s\n", hex_off(off).c_str(), f.bytes,
                  f.name.c_str(), f.encoding.c_str());
    t += buf;
    off += f.bytes;
  }

  std::snprintf(buf, sizeof(buf), "\n── gain(%d×%d,Q10;物理值)──\n", gain_w, gain_h);
  t += buf;
  const auto gain_table = [&](const char* name, const std::vector<double>& g) {
    t += std::string(name) + ":\n";
    for (int r = 0; r < gain_h; ++r) {
      for (int c = 0; c < gain_w; ++c) {
        std::snprintf(buf, sizeof(buf), " %6.3f",
                      g[static_cast<size_t>(r) * static_cast<size_t>(gain_w) +
                        static_cast<size_t>(c)]);
        t += buf;
      }
      t += "\n";
    }
  };
  gain_table("left", gain_l);
  gain_table("right", gain_r);

  std::snprintf(buf, sizeof(buf), "\n── DCC(%d×%d,單位 %s,Q%d)──\n物理值:\n", dcc_w, dcc_h,
                meta.dcc_unit.c_str(), q_format);
  t += buf;
  for (int r = 0; r < dcc_h; ++r) {
    for (int c = 0; c < dcc_w; ++c) {
      std::snprintf(buf, sizeof(buf), " %8.4f",
                    dcc[static_cast<size_t>(r) * static_cast<size_t>(dcc_w) +
                        static_cast<size_t>(c)]);
      t += buf;
    }
    t += "\n";
  }
  t += "編碼值(hex):\n";
  for (int r = 0; r < dcc_h; ++r) {
    for (int c = 0; c < dcc_w; ++c) {
      const double v = dcc[static_cast<size_t>(r) * static_cast<size_t>(dcc_w) +
                           static_cast<size_t>(c)];
      t += " " + hex16(dcc::eeprom::encode_q(v, q_format));
    }
    t += "\n";
  }

  std::snprintf(buf, sizeof(buf), "\nchecksum: %u(%s,Σ前段 %% 256)\n",
                static_cast<unsigned>(blob.back()), hex_off(blob.back()).c_str());
  t += buf;
  return t;
}

}  // namespace dcc::io

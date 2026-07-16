# EEPROM 可讀等價輸出 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `block.bin` 之外另落盤逐欄對應的 `block.json` / `block.txt` 可讀等價檔,並記錄格式凍結與 EEPROM 假設格式轉正之 spec 勘誤。

**Architecture:** 新增 `dcc_io/eeprom_equiv`(純字串建構、無檔案 I/O),輸入與 `dcc_core::eeprom::pack()` 相同;`RunResult` 增加打包輸入回聲欄位;session 與 GUI 落盤收斂為 `dcc::app::write_output_files()` 共用路徑。

**Tech Stack:** C++17、nlohmann_json(dcc_io 既有 PRIVATE 依賴)、Catch2。

## Global Constraints

- 分層:gui/cli → app → io/hal → core,嚴格單向(CLAUDE.md 鐵律 2);core 不得引入 json 依賴。
- 單位契約:dcc 傳入等價檔建構器前已依 `output_disparity_unit` 轉出(與 pack 同);跨模組函式 docstring 首行標注單位(鐵律 1)。
- 確定性輸出:block.json/txt **不含時間戳**(與 report.json 同準則,IT-06 bit-exact)。
- 驗算文化:layout bytes 總和必須 == block.bin 實際長度(993 = 2+4+442+442+2+4+96+1)。
- 註解/訊息 Traditional Chinese;識別字與 commit English;commit 前綴 `M1:`(spec 改動 `spec:`)。
- 每 Task 完成:`cmake --build build`(本專案 targets 零警告)→ `ctest --test-dir build` 全綠。

---

### Task 1: dcc_io/eeprom_equiv 建構器(TDD)

**Files:**
- Create: `tests/eeprom_equiv_test.cpp`
- Create: `src/dcc_io/include/dcc_io/eeprom_equiv.hpp`
- Create: `src/dcc_io/src/eeprom_equiv.cpp`
- Modify: `src/dcc_io/CMakeLists.txt`(add source)
- Modify: `tests/CMakeLists.txt`(add test file)

**Interfaces:**
- Consumes: `dcc_core/eeprom_codec.hpp` 之 `pack/encode_q/kBlockVersion`
- Produces(Task 3 依賴):
  ```cpp
  namespace dcc::io {
  struct BlockEquivMeta { std::string module_id, config_hash, dcc_unit; };
  std::string build_block_json(const BlockEquivMeta&, const std::vector<double>& gain_l,
                               const std::vector<double>& gain_r, int gain_w, int gain_h,
                               const std::vector<double>& dcc, int dcc_w, int dcc_h, int q_format);
  std::string build_block_txt(/* 同上 */);
  }
  ```

- [x] **Step 1: 寫失敗測試** — `tests/eeprom_equiv_test.cpp`:

```cpp
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
```

`tests/CMakeLists.txt` 之 `add_executable(dcc_tests ...)` 清單加一行 `eeprom_equiv_test.cpp`(放在 `it_pipeline_test.cpp` 之前)。

- [x] **Step 2: 跑測試確認失敗**

Run: `cmake --build build && ctest --test-dir build -R "equiv:"`
Expected: 編譯失敗(`dcc_io/eeprom_equiv.hpp` 不存在)——即 TDD 紅燈。

- [x] **Step 3: 最小實作**

`src/dcc_io/include/dcc_io/eeprom_equiv.hpp`:

```cpp
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
```

`src/dcc_io/src/eeprom_equiv.cpp`:

```cpp
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
```

`src/dcc_io/CMakeLists.txt` 的 `add_library(dcc_io STATIC ...)` 加一行 `src/eeprom_equiv.cpp`。

- [x] **Step 4: 跑測試確認通過**

Run: `cmake --build build && ctest --test-dir build -R "equiv:"`
Expected: 2 案例 PASS,build 本專案 targets 零警告。

- [x] **Step 5: Commit**

```bash
git add tests/eeprom_equiv_test.cpp tests/CMakeLists.txt \
        src/dcc_io/include/dcc_io/eeprom_equiv.hpp src/dcc_io/src/eeprom_equiv.cpp \
        src/dcc_io/CMakeLists.txt
git commit -m "M1: dcc_io eeprom_equiv - readable block.json/txt builders (TDD)"
```

---

### Task 2: RunResult 打包輸入回聲 + 共用落盤路徑 + IT 五檔測試

**Files:**
- Modify: `src/dcc_app/include/dcc_app/pipeline.hpp`(RunResult 增欄位)
- Modify: `src/dcc_app/src/pipeline.cpp`(Phase G 填欄位,約 :92-99)
- Modify: `src/dcc_app/include/dcc_app/session.hpp`(新增 `write_output_files`)
- Modify: `src/dcc_app/src/session.cpp`(實作 + run_session 改用)
- Modify: `src/dcc_gui/panels.cpp`(:598-606 `do_write` 改用共用函式)
- Modify: `src/dcc_cli/main.cpp`(:123 輸出訊息更新)
- Modify: `tests/it_pipeline_test.cpp`(新增五檔 IT 案例)

**Interfaces:**
- Consumes: Task 1 之 `dcc::io::build_block_json/build_block_txt/BlockEquivMeta`
- Produces:
  ```cpp
  // RunResult 新欄位(pipeline.hpp)
  std::vector<double> dcc_out;        // Phase G 打包值(依 output_disparity_unit)
  std::string dcc_out_unit;           // "DAC/raw_pixel" | "DAC/pd_image_grid"
  std::vector<double> gain_l, gain_r; // 打包 gain 回聲
  int gain_w = 0, gain_h = 0;
  // session.hpp
  void write_output_files(const std::string& out_dir, const dcc::io::AppConfig& cfg,
                          const RunResult& res, const std::string& report_json);
  ```

- [x] **Step 1: 寫失敗測試** — `tests/it_pipeline_test.cpp` 檔尾新增:

```cpp
TEST_CASE("IT: 落盤五檔齊全且 block.json 與 block.bin 等價(checksum/長度閉環)", "[it_out]") {
  const auto cfg = app_cfg();
  const auto dir = tmp_dir("it_out5");
  const auto out = dcc::app::run_session(cfg, generate(base_spec(cfg)), dir.string());
  REQUIRE(out.completed);
  for (const char* f : {"report.json", "report.md", "block.bin", "block.json", "block.txt"})
    REQUIRE(fs::exists(dir / f));

  std::ifstream blk(dir / "block.bin", std::ios::binary);
  const std::vector<char> bin((std::istreambuf_iterator<char>(blk)),
                              std::istreambuf_iterator<char>());
  std::ifstream bj(dir / "block.json");
  const auto j = nlohmann::json::parse(bj);
  REQUIRE(j["total_bytes"].get<size_t>() == bin.size());
  REQUIRE(j["checksum"]["value"].get<int>() ==
          static_cast<int>(static_cast<unsigned char>(bin.back())));
  REQUIRE(j["dcc"]["unit"] == "DAC/raw_pixel");  // config 預設 output_disparity_unit
}
```

檔頭 include 若缺 `<fstream>`、`<iterator>`、`<vector>` 則補上。

- [x] **Step 2: 跑測試確認失敗**

Run: `cmake --build build && ctest --test-dir build -R "落盤五檔"`
Expected: FAIL(`block.json` 不存在)。

- [x] **Step 3: 實作**

(a) `pipeline.hpp` — `RunResult` 內 `std::vector<std::uint8_t> block;` 之後加:

```cpp
  // Phase G 打包輸入回聲(可讀等價檔 block.json/txt 用;單位已依 output_disparity_unit 轉出)
  std::vector<double> dcc_out;
  std::string dcc_out_unit;            // "DAC/raw_pixel" | "DAC/pd_image_grid"
  std::vector<double> gain_l, gain_r;  // 線性增益
  int gain_w = 0, gain_h = 0;
```

(b) `pipeline.cpp` Phase G — `res.block = ...; verify` 之後、`return res;` 之前加:

```cpp
  res.dcc_out = std::move(dcc_out);
  res.dcc_out_unit = (cfg.output_disparity_unit == "pd_image_grid") ? "DAC/pd_image_grid"
                                                                    : "DAC/raw_pixel";
  res.gain_l = gain_l;
  res.gain_r = gain_r;
  res.gain_w = gain_w;
  res.gain_h = gain_h;
```

(注意 `pack(...)` 呼叫在前,`std::move(dcc_out)` 在後,順序安全。)

(c) `session.hpp` — `run_session` 宣告前加:

```cpp
// 落盤全部輸出:report.json/md + block.bin + 可讀等價 block.json/txt(SPEC-004 §4a)。
// session 與 GUI 共用;out_dir 不存在時自動建立。
void write_output_files(const std::string& out_dir, const dcc::io::AppConfig& cfg,
                        const RunResult& res, const std::string& report_json);
```

(d) `session.cpp` — include 加 `#include "dcc_io/eeprom_equiv.hpp"`;新增函式(置於 `run_session` 前):

```cpp
void write_output_files(const std::string& out_dir, const dcc::io::AppConfig& cfg,
                        const RunResult& res, const std::string& report_json) {
  namespace fs = std::filesystem;
  fs::create_directories(out_dir);
  write_file(fs::path(out_dir) / "report.json", report_json);
  write_file(fs::path(out_dir) / "report.md", build_report_md(cfg, res));
  std::ofstream blk(fs::path(out_dir) / "block.bin", std::ios::binary);
  blk.write(reinterpret_cast<const char*>(res.block.data()),
            static_cast<std::streamsize>(res.block.size()));
  const dcc::io::BlockEquivMeta meta{res.module_id, cfg.hash, res.dcc_out_unit};
  write_file(fs::path(out_dir) / "block.json",
             dcc::io::build_block_json(meta, res.gain_l, res.gain_r, res.gain_w, res.gain_h,
                                       res.dcc_out, cfg.grid_w, cfg.grid_h, cfg.q_format));
  write_file(fs::path(out_dir) / "block.txt",
             dcc::io::build_block_txt(meta, res.gain_l, res.gain_r, res.gain_w, res.gain_h,
                                      res.dcc_out, cfg.grid_w, cfg.grid_h, cfg.q_format));
}
```

`run_session` 內原 `if (!out_dir.empty()) { ... }` 成功分支(:114-121)改為:

```cpp
    if (!out_dir.empty()) write_output_files(out_dir, cfg, res, out.report_json);
```

(e) `panels.cpp` `do_write`(:597-606)改為:

```cpp
  const auto do_write = [&]() {
    dcc::app::write_output_files(out_dir, s.cfg, s.result, s.report_json);
    s.log_add(LogLevel::info, std::string("報告已落盤:") + out_dir +
                                  "/report.{json,md} + block.{bin,json,txt}");
  };
```

(若 `panels.cpp` 因此不再使用 `<fstream>`/`<filesystem>` 於此函式外,include 保留不動——其他處仍在用。)

(f) `src/dcc_cli/main.cpp:123` 訊息改為:

```cpp
      std::printf("輸出:%s/report.json、report.md、block.bin、block.json、block.txt\n", out_dir);
```

- [x] **Step 4: 跑測試確認通過**

Run: `cmake --build build && ctest --test-dir build`
Expected: 全綠(76 + Task 1 兩案 + 本案 = 79);build 本專案 targets 零警告。

再驗 CLI/GUI 實路徑:
Run: `./build/src/dcc_cli/dcc_cal --dry-run --out /tmp/dcc_equiv_check && ls /tmp/dcc_equiv_check`
Expected: 列出五檔;`block.txt` 含 8×6 兩張表。
Run: `./build/src/dcc_gui/dcc_gui --smoke`
Expected: 正常結束。

- [x] **Step 5: Commit**

```bash
git add src/dcc_app src/dcc_gui/panels.cpp src/dcc_cli/main.cpp tests/it_pipeline_test.cpp
git commit -m "M1: readable block.json/txt output; unify session/GUI output path"
```

---

### Task 3: Spec 勘誤 + 開發紀錄 + CLAUDE.md

**Files:**
- Modify: `specs/SPEC-004_資料格式.md`
- Modify: `specs/SPEC-005_驗證與測試計畫.md`
- Modify: `docs/開發紀錄_M0-M1.md`
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-07-16-eeprom-equiv-output-design.md`(時間戳勘誤)

**Interfaces:** 無程式介面;內容依 2026-07-16 決議(見設計文件「背景與決議」)。

- [x] **Step 1: SPEC-004 勘誤**(頂部 revision 各加一行,日期 2026-07-16)

1. §3a 標題與 §3a.1 標題之「(提案,待 M2 與外部 SAD 模組凍結)」改「(已凍結 2026-07-16)」,
   §3a.1 開頭之提案聲明段改為對照表:

```markdown
> **凍結記錄(2026-07-16)**:disp_seq.json v1 欄位結構/單位/必填規則已與外部 SAD 團隊凍結。
> quality 數值語意**未採**本節原提案(q ∝ 1/σ²),對方標定為 **SAD 曲線形狀導出
> (極小值尖銳度/峰谷比類)**;具體公式與值域文件待補,補齊後更新下表。
>
> | 面向 | 本工具原提案 | 對方凍結標定 |
> |---|---|---|
> | 來源 | σ 單調遞減代理,q ∝ 1/σ² | SAD 極小值尖銳度/峰谷比 |
> | 對 D-5 聚合 | 最小變異加權 | 單調信心權重(仍可用) |
> | 對 M2 WLS/EIV | q 可直接推 σ | **須先建立 q→σ 標定才可作權重** |
```

(原提案內文保留作歷史依據,前綴「原提案內容:」。)

2. §4 表格後之引言區改為:

```markdown
> 本專案非 Qualcomm 專用(2026-07-16 決議):本開發版 layout 即正式輸出格式;
> `PDAFCalibrationTools_EEPROM.h` 逐位對齊改為**條件式**——僅對接 Qualcomm 平台時執行,差異以對照表記錄。
> 離線前期:pack/verify 落盤 `block.bin` 並經 SimNvm 回讀驗證;實體燒錄屬 M2。
```

3. §4 之後新增 §4a:

```markdown
### 4a. 可讀等價檔 block.json / block.txt(2026-07-16 新增)

與 `block.bin` 同目錄輸出、同源生成(相同 pack 輸入),供跨平台整合與人工核對:

- `block.json`:`format="dcc-block-equiv v1"`;含 block_version、module_id、config_hash、
  `layout[]`(每欄 field/offset/bytes/encoding)、`total_bytes`、
  gain(w/h/left/right 物理值)、dcc(w/h/unit/q_format/values 物理值/encoded_hex)、
  checksum(value/hex/rule)。**確定性輸出,無時間戳**(同 §5 report.json 準則)。
- `block.txt`:人閱讀版(繁中);layout 表 + gain 表 + DCC 物理值/hex 兩張 8×6 表 + checksum。
- 等價性守則:`total_bytes` 與 `checksum` 由 `eeprom::pack()` 同源取得,
  layout bytes 總和 == block.bin 長度(993),由 UT 閉環。
```

- [x] **Step 2: SPEC-005 勘誤**(頂部 revision 加一行)

1. §7 開放問題 #4(實際 EEPROM layout)改:「**改性質(2026-07-16)**:本專案非 Qualcomm 專用,
   開發版 layout 轉正;`PDAFCalibrationTools_EEPROM.h` 對齊為條件式(對接 Qualcomm 才需)。」
2. §7 開放問題中關於外部格式凍結者(若列)標已關閉(2026-07-16)。
3. 測試對映新增一行:UT 補充 `eeprom_equiv`(block.json/txt 等價閉環)、IT 補充五檔落盤。

- [x] **Step 3: 開發紀錄與 CLAUDE.md**

`docs/開發紀錄_M0-M1.md`:
- §5 介面契約:「尚未與外部 SAD 團隊正式凍結——M2 前必辦」改
  「**已凍結(2026-07-16)**;quality 語意依對方標定(SAD 曲線形狀導出),對照表見 SPEC-004 §3a.1;
  M2 WLS/EIV 前須先建 q→σ 標定」。
- §7 M2 待辦:#1 標「✅ 完成(2026-07-16)」;#3 改
  「EEPROM 假設格式已轉正(非 Qualcomm 專用);另有 block.json/txt 可讀等價檔;
  NvmIF 實體實作與(條件式)Qualcomm 對齊屬 M2」。
- §6 與測試數字:`76/76` 更新為 Task 2 後之實際數字(ctest 輸出為準)。

`CLAUDE.md`:
- 「開放問題」#2 保留;#4 改為「已改性質 2026-07-16:假設格式轉正,Qualcomm 對齊為條件式」;
  外部凍結相關敘述(「介面保留待 M2 對接」處不動)。
- 「目前狀態」段測試數字同步更新。

`docs/superpowers/specs/2026-07-16-eeprom-equiv-output-design.md`:
- 「meta:…時間戳…」改為「(無時間戳——確定性輸出,依 IT-06 準則)」。

- [x] **Step 4: 驗證**

Run: `cmake --build build && ctest --test-dir build && ./build/src/dcc_gui/dcc_gui --smoke && ./build/src/dcc_seqview/dcc_seqview --smoke`
Expected: 全綠、smoke OK(文件改動不應影響)。

- [x] **Step 5: Commit**

```bash
git add specs/SPEC-004_資料格式.md specs/SPEC-005_驗證與測試計畫.md \
        docs/開發紀錄_M0-M1.md CLAUDE.md docs/superpowers/specs/2026-07-16-eeprom-equiv-output-design.md
git commit -m "spec: freeze disp_seq v1 (quality per SAD-shape calibration); adopt dev EEPROM layout as official; document block.json/txt"
```

---

### Task 4: 最終驗證

- [x] **Step 1: 全量驗證(CLAUDE.md 最低驗證)**

```bash
cmake --build build            # 本專案 targets 零警告
ctest --test-dir build         # 全綠(預期 79)
./build/src/dcc_cli/dcc_cal --dry-run --out /tmp/dcc_final_check
ls /tmp/dcc_final_check        # 五檔齊全
./build/src/dcc_gui/dcc_gui --smoke
./build/src/dcc_seqview/dcc_seqview --smoke
```

- [x] **Step 2: 人工抽查 block.txt**

Run: `cat /tmp/dcc_final_check/block.txt`
Expected: layout 表 offset 0x0000→0x03E0;DCC 中央值 ≈ 12.46 對應 hex ≈ 0x031D;checksum 行存在。

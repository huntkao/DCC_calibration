// Session 層:執行管線 + 報告落盤 + 失敗現場落盤(鐵律 4)。
#pragma once

#include <string>

#include "dcc_app/pipeline.hpp"
#include "dcc_io/logging.hpp"

namespace dcc::app {

struct SessionOutcome {
  bool completed = false;     // 管線是否跑完(判定 FAIL 也算跑完)
  bool pass = false;          // 模組判定
  std::string report_json;    // 完整報告(completed 時有值;確定性輸出)
  std::string error_code;     // 中止時之錯誤碼(如 "E-E01")
  std::string error_msg;      // 中止訊息(繁中)
};

// 建構 report JSON(SPEC-004 §5;無時間戳,同輸入必 bit-exact,IT-06)。
std::string build_report_json(const dcc::io::AppConfig& cfg, const RunResult& res);

// 建構人閱讀版報告(Markdown,繁中)。
std::string build_report_md(const dcc::io::AppConfig& cfg, const RunResult& res);

// 落盤全部輸出:report.json/md + block.bin + 可讀等價 block.json/txt(SPEC-004 §4a)。
// session 與 GUI 共用;out_dir 不存在時自動建立。
void write_output_files(const std::string& out_dir, const dcc::io::AppConfig& cfg,
                        const RunResult& res, const std::string& report_json);

// 執行管線;out_dir 非空時落盤 report.json / report.md / block.bin,
// 中止時落盤 abort_dump.json(config 快照 + 原始序列 + 錯誤),不上拋。
// gain map 前期以 SimNvm 平坦值(1.0×221)透傳。
SessionOutcome run_session(const dcc::io::AppConfig& cfg, const std::string& disp_seq_json,
                           const std::string& out_dir, dcc::io::Logger* logger = nullptr);

}  // namespace dcc::app

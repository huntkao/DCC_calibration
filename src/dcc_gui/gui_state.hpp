// GUI 狀態(單一真值來源):Sim 參數 + config → dirty → 重算 → 快照。
// 解耦紀律:本層只呼叫 dcc_sim / dcc_app,不含任何演算法。
#pragma once

#include <string>
#include <vector>

#include "dcc_app/pipeline.hpp"
#include "dcc_io/config.hpp"
#include "dcc_sim/synth.hpp"

namespace dcc::gui {

enum class LogLevel { info = 0, warn = 1, error = 2 };

struct LogEntry {
  LogLevel level;
  std::string msg;
};

struct GuiState {
  // ── 輸入(可編輯;任何修改須設 dirty)────────────────────────────────
  dcc::io::AppConfig cfg;      // 載入後可於 Config 面板局部修改
  dcc::sim::SynthSpec spec;    // Sim 工作台參數
  int null_frames = 0;         // 快速注入:角落區 (5,7) 前 N 幀 null
  bool fine_grid = false;      // true → 144×108 細粒度(行使 D-5 聚合)

  bool auto_run = true;
  bool dirty = true;

  // ── 輸出(唯讀快照)──────────────────────────────────────────────────
  bool has_result = false;     // 管線跑完(判定 FAIL 也算)
  dcc::app::RunResult result;
  std::string error_code, error_msg;  // 中止時
  std::string last_seq_json;   // 最近一次生成之序列(存檔用)

  // ── 靈敏度掃描(開放問題 #3:合焦偏移 vs DCC/err)────────────────────
  struct ScanPoint {
    double offset = 0.0;       // 合焦偏移 [DAC]
    double central_dcc = 0.0;  // 中央 4 區平均 DCC
    double delta_pct = 0.0;    // 相對 offset=0 之 DCC 變化 [%]
    double max_err = 0.0;      // 全區最大 err
    std::string error;         // 中止時之 E-code(空 = 正常)
  };
  double scan_range = 60.0;    // ±offset [DAC]
  int scan_steps = 25;
  std::vector<ScanPoint> scan;

  // ── UI 雜項 ─────────────────────────────────────────────────────────
  int sel_r = 2, sel_c = 3;    // 區域檢視選擇
  std::vector<LogEntry> log;
  bool log_autoscroll = true;

  void log_add(LogLevel lv, std::string msg);
  // 重生成合成序列並執行管線;結果/錯誤寫回快照,清除 dirty。
  void regenerate_and_run();
  // 以目前 Sim/config 參數執行合焦偏移掃描(±scan_range, scan_steps 點)。
  void run_scan();
};

}  // namespace dcc::gui

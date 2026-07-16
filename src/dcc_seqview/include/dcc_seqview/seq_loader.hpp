// 獨立於 dcc_core/dcc_io——刻意零耦合校正引擎與管線,供外部團隊單獨建置,
// 離線檢視/初步檢核自產 disp_seq.json(SPEC-004 §3a),不需 build 完整校正工具。
// 讀取「原始粒度」(不做 D-5 聚合、D-6 統計、單位轉換);診斷性檢查而非管線之
// 嚴格 D-1..D-6 判定——結構不足以繪圖才 ok=false,其餘問題一律記為 issue 仍可瀏覽,
// 讓外部團隊在送出序列前先看見自己資料的毛病。
#pragma once

#include <string>
#include <vector>

namespace dcc::seqview {

enum class Severity { warn, error };

struct Issue {
  Severity severity;
  std::string message;
};

struct RawSeq {
  std::string module_id;
  std::string unit;       // 原樣字串(未白名單校驗、未做單位轉換)
  int pitch_x = 0;
  std::vector<int> dacs;  // [N]
  int grid_w = 0, grid_h = 0;
  // 逐幀 [grid_h*grid_w] 展平,row-major;NaN = null 或解析失敗之佔位。
  std::vector<std::vector<double>> data, focus, quality;
  bool has_quality = false;
};

struct LoadResult {
  bool ok = false;            // false = 結構不足以繪圖(json 語法錯誤/缺 dacs/grid 非法)
  RawSeq seq;
  std::vector<Issue> issues;  // error 與 warn 皆收集,不中斷
};

LoadResult load(const std::string& json_text);
LoadResult load_file(const std::string& path);

}  // namespace dcc::seqview

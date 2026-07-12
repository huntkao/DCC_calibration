// 結構化 logging(NFR-03/NFR-05):雙 sink 落盤——
//   dcc_<ts>.log   人讀版(繁中,tab 分欄)
//   dcc_<ts>.jsonl 結構化版(每行一筆 JSON,離線分析用)
// 欄位:ts / level / phase / code / msg。執行緒安全。
#pragma once

#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace dcc::io {

class Logger {
 public:
  // 於 dir 下建立成對檔案;失敗回 nullptr(呼叫端降級為僅記憶體 log)。
  static std::unique_ptr<Logger> create(const std::string& dir);

  // level: "info" | "warn" | "error";phase: "A".."G" 或空;code: "E-xx" 或空。
  void log(const std::string& level, const std::string& phase, const std::string& code,
           const std::string& msg);

  const std::string& base_path() const { return base_; }

 private:
  std::ofstream human_, jsonl_;
  std::string base_;
  std::mutex mu_;
};

}  // namespace dcc::io

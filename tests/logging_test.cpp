// 結構化 logging(M1d):雙 sink 落盤、jsonl 可解析。
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "dcc_io/logging.hpp"

namespace fs = std::filesystem;

TEST_CASE("Logger:雙 sink 建檔,jsonl 每行可解析且欄位齊全", "[logging]") {
  const fs::path dir = fs::temp_directory_path() / "dcc_log_test";
  fs::remove_all(dir);

  auto lg = dcc::io::Logger::create(dir.string());
  REQUIRE(lg != nullptr);
  lg->log("info", "A", "", "run 開始");
  lg->log("error", "D", "E-D03", "區域有效樣本不足");

  REQUIRE(fs::exists(lg->base_path() + ".log"));
  REQUIRE(fs::exists(lg->base_path() + ".jsonl"));

  std::ifstream f(lg->base_path() + ".jsonl");
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    const auto j = nlohmann::json::parse(line);  // 每行必為合法 JSON
    REQUIRE(j.contains("ts"));
    REQUIRE(j.contains("level"));
    REQUIRE(j.contains("msg"));
    ++n;
  }
  REQUIRE(n == 2);

  // 人讀版含錯誤碼欄位。
  std::ifstream h(lg->base_path() + ".log");
  std::stringstream ss;
  ss << h.rdbuf();
  REQUIRE(ss.str().find("E-D03") != std::string::npos);
}

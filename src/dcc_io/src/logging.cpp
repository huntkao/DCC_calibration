#include "dcc_io/logging.hpp"

#include <ctime>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace dcc::io {

namespace {
std::string now_string() {
  char buf[32];
  const std::time_t t = std::time(nullptr);
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  return buf;
}
std::string now_compact() {
  char buf[32];
  const std::time_t t = std::time(nullptr);
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&t));
  return buf;
}
}  // namespace

std::unique_ptr<Logger> Logger::create(const std::string& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  auto lg = std::make_unique<Logger>();
  lg->base_ = dir + "/dcc_" + now_compact();
  lg->human_.open(lg->base_ + ".log", std::ios::app);
  lg->jsonl_.open(lg->base_ + ".jsonl", std::ios::app);
  if (!lg->human_ || !lg->jsonl_) return nullptr;
  return lg;
}

void Logger::log(const std::string& level, const std::string& phase, const std::string& code,
                 const std::string& msg) {
  const std::string ts = now_string();
  std::lock_guard<std::mutex> lock(mu_);
  human_ << ts << '\t' << level << '\t' << (phase.empty() ? "-" : phase) << '\t'
         << (code.empty() ? "-" : code) << '\t' << msg << '\n';
  human_.flush();

  nlohmann::json j;
  j["ts"] = ts;
  j["level"] = level;
  if (!phase.empty()) j["phase"] = phase;
  if (!code.empty()) j["code"] = code;
  j["msg"] = msg;
  jsonl_ << j.dump() << '\n';
  jsonl_.flush();
}

}  // namespace dcc::io

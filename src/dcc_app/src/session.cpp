#include "dcc_app/session.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "dcc_core/error.hpp"
#include "dcc_core/units.hpp"

namespace dcc::app {

namespace {

using nlohmann::json;

json grid_field(const std::vector<RegionResult>& regions, double RegionResult::*member) {
  json arr = json::array();
  for (const auto& r : regions) arr.push_back(r.*member);
  return arr;
}

void write_file(const std::filesystem::path& p, const std::string& content) {
  std::ofstream f(p, std::ios::binary);
  f << content;
}

}  // namespace

std::string build_report_json(const dcc::io::AppConfig& cfg, const RunResult& res) {
  json j;
  j["module_id"] = res.module_id;
  j["config_hash"] = cfg.hash;
  j["config"] = json::parse(cfg.snapshot);
  j["sweep"]["dacs"] = res.dacs;
  j["sweep"]["span"] = res.span;

  j["disparity"]["unit"] = "raw_pixel";  // core 單一單位(SPEC-003 §3)
  j["disparity"]["data"] = res.seq.disp;
  if (!res.seq.quality.empty()) j["disparity"]["quality"] = res.seq.quality;
  j["focus"]["fv"] = res.seq.focus;
  j["focus"]["peak"] = grid_field(res.regions, &RegionResult::focus_peak);

  auto& r = j["result"];
  r["dcc_raw_px"] = grid_field(res.regions, &RegionResult::dcc_raw_px);
  r["dcc_pd_grid"] = grid_field(res.regions, &RegionResult::dcc_pd_grid);
  r["intercept"] = grid_field(res.regions, &RegionResult::intercept);
  r["r2"] = grid_field(res.regions, &RegionResult::r2);
  r["err"] = grid_field(res.regions, &RegionResult::err);
  r["pass"] = res.pass;
  // 16 倍檢核(SPEC-004 §3):恆等 pitch_x,否則示警。
  const auto rc = dcc::units::dcc_ratio_check(res.regions[0].dcc_raw_px,
                                              res.regions[0].dcc_pd_grid, cfg.pitch_x);
  r["dcc_ratio_check"] = (rc == dcc::units::RatioCheck::ok) ? "ok" : "warn";
  r["worst"] = json::array();
  for (const auto& w : res.worst) {
    const size_t rr = w.index / static_cast<size_t>(cfg.grid_w);
    const size_t cc = w.index % static_cast<size_t>(cfg.grid_w);
    r["worst"].push_back({{"r", rr}, {"c", cc}, {"err", w.err}});
  }
  j["warnings"] = res.warnings;
  return j.dump(2);
}

std::string build_report_md(const dcc::io::AppConfig& cfg, const RunResult& res) {
  std::string md;
  md += "# DCC 校正報告 — " + res.module_id + "\n\n";
  md += "- 判定:**" + std::string(res.pass ? "PASS" : "FAIL") + "**\n";
  md += "- config hash:`" + cfg.hash + "`\n";
  md += "- sweep:" + std::to_string(res.dacs.front()) + " → " + std::to_string(res.dacs.back()) +
        "(span " + std::to_string(static_cast<int>(res.span)) + " DAC,10 點)\n";
  const size_t center = static_cast<size_t>(cfg.grid_h / 2) * static_cast<size_t>(cfg.grid_w) +
                        static_cast<size_t>(cfg.grid_w / 2);
  md += "- 中央區 DCC:" + std::to_string(res.regions[center].dcc_raw_px) + " DAC/raw_px\n";
  md += "- 最差三區:";
  for (const auto& w : res.worst)
    md += "(r=" + std::to_string(w.index / static_cast<size_t>(cfg.grid_w)) +
          ",c=" + std::to_string(w.index % static_cast<size_t>(cfg.grid_w)) +
          " err=" + std::to_string(w.err) + ")";
  md += "\n- 警告數:" + std::to_string(res.warnings.size()) + "\n";
  return md;
}

SessionOutcome run_session(const dcc::io::AppConfig& cfg, const std::string& disp_seq_json,
                           const std::string& out_dir) {
  namespace fs = std::filesystem;
  SessionOutcome out;
  // 前期 gain map:SimNvm 平坦值透傳(13×17)。
  const std::vector<double> flat_gain(221, 1.0);

  try {
    const RunResult res = run(cfg, disp_seq_json, flat_gain, flat_gain);
    out.completed = true;
    out.pass = res.pass;
    out.report_json = build_report_json(cfg, res);
    if (!out_dir.empty()) {
      fs::create_directories(out_dir);
      write_file(fs::path(out_dir) / "report.json", out.report_json);
      write_file(fs::path(out_dir) / "report.md", build_report_md(cfg, res));
      std::ofstream blk(fs::path(out_dir) / "block.bin", std::ios::binary);
      blk.write(reinterpret_cast<const char*>(res.block.data()),
                static_cast<std::streamsize>(res.block.size()));
    }
  } catch (const DccError& e) {
    // 鐵律 4:先落盤現場資料再回報。
    out.error_code = to_string(e.code());
    out.error_msg = e.what();
    if (!out_dir.empty()) {
      fs::create_directories(out_dir);
      json dump;
      dump["error"] = {{"code", out.error_code}, {"msg", out.error_msg}};
      dump["config_hash"] = cfg.hash;
      dump["config"] = json::parse(cfg.snapshot);
      dump["disp_seq_raw"] = disp_seq_json;  // 原始序列全文(可離線重算)
      write_file(fs::path(out_dir) / "abort_dump.json", dump.dump(2));
    }
  }
  return out;
}

}  // namespace dcc::app

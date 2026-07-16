#include "dcc_seqview/seq_loader.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include <nlohmann/json.hpp>

namespace dcc::seqview {

namespace {

using nlohmann::json;
constexpr size_t kAbsent = std::numeric_limits<size_t>::max();  // 欄位不存在之哨兵值

void add(std::vector<Issue>& issues, Severity sev, std::string msg) {
  issues.push_back({sev, std::move(msg)});
}

// 解析單一幀 [grid_h][grid_w] 平面;形狀或元素不符則整幀以 NaN 填(記 issue,不中斷其他幀)。
std::vector<double> parse_plane(const json& frame, int grid_w, int grid_h, const char* field,
                                size_t frame_idx, std::vector<Issue>& issues) {
  const size_t cells = static_cast<size_t>(grid_w) * static_cast<size_t>(grid_h);
  std::vector<double> plane(cells, std::nan(""));

  const auto bad = [&] {
    add(issues, Severity::error, std::string(field) + " 第 " + std::to_string(frame_idx) +
                                     " 幀形狀或元素與 grid 不符,以 NaN 面代替");
  };
  if (!frame.is_array() || frame.size() != static_cast<size_t>(grid_h)) { bad(); return plane; }

  for (int r = 0; r < grid_h; ++r) {
    const json& row = frame[static_cast<size_t>(r)];
    if (!row.is_array() || row.size() != static_cast<size_t>(grid_w)) { bad(); return plane; }
    for (int c = 0; c < grid_w; ++c) {
      const json& v = row[static_cast<size_t>(c)];
      if (v.is_null()) continue;  // 已預設 NaN
      if (!v.is_number()) { bad(); return plane; }
      plane[static_cast<size_t>(r) * static_cast<size_t>(grid_w) + static_cast<size_t>(c)] =
          v.get<double>();
    }
  }
  return plane;
}

// 欄位存在且為 array → 回傳其幀數;不存在 → kAbsent(不構成幀數約束)。
size_t frame_count(const json& j, const char* key) {
  return (j.contains(key) && j[key].is_array()) ? j[key].size() : kAbsent;
}

}  // namespace

LoadResult load(const std::string& json_text) {
  LoadResult out;
  json j;
  try {
    j = json::parse(json_text);
  } catch (const json::parse_error& e) {
    add(out.issues, Severity::error, std::string("JSON 解析失敗:") + e.what());
    return out;
  }

  RawSeq& seq = out.seq;

  if (j.contains("module_id") && j["module_id"].is_string())
    seq.module_id = j["module_id"].get<std::string>();
  else
    add(out.issues, Severity::warn, "缺 module_id 或非字串");

  if (j.contains("unit") && j["unit"].is_string()) {
    seq.unit = j["unit"].get<std::string>();
    if (seq.unit != "raw_pixel" && seq.unit != "pd_image_grid")
      add(out.issues, Severity::warn, "unit 非 SPEC-004 白名單值:" + seq.unit);
  } else {
    seq.unit = "raw_pixel";
    add(out.issues, Severity::warn, "缺 unit,座標軸標籤以 raw_pixel 顯示");
  }

  if (j.contains("pitch_x") && j["pitch_x"].is_number())
    seq.pitch_x = j["pitch_x"].get<int>();
  else
    add(out.issues, Severity::warn, "缺 pitch_x 或非數值");

  // dacs:必要且須 ≥2 點,否則無法建軸 → 結構不足以繪圖。
  if (!j.contains("dacs") || !j["dacs"].is_array() || j["dacs"].size() < 2) {
    add(out.issues, Severity::error, "缺 dacs 或長度不足 2,無法繪圖");
    return out;
  }
  for (const auto& d : j["dacs"]) {
    if (!d.is_number()) {
      add(out.issues, Severity::error, "dacs 含非數值元素,無法繪圖");
      return out;
    }
    seq.dacs.push_back(d.get<int>());
  }
  bool monotonic = true;
  for (size_t i = 1; i < seq.dacs.size(); ++i)
    if (seq.dacs[i] <= seq.dacs[i - 1]) { monotonic = false; break; }
  if (!monotonic)
    add(out.issues, Severity::error, "dacs 非嚴格遞增(FAR→NEAR,鐵律 3)");

  // grid_w/grid_h:必要且須為正,否則無法建平面 → 結構不足以繪圖。
  const int gw = (j.contains("grid_w") && j["grid_w"].is_number()) ? j["grid_w"].get<int>() : 0;
  const int gh = (j.contains("grid_h") && j["grid_h"].is_number()) ? j["grid_h"].get<int>() : 0;
  if (gw <= 0 || gh <= 0) {
    add(out.issues, Severity::error, "grid_w/grid_h 缺失或非正,無法繪圖");
    return out;
  }
  seq.grid_w = gw;
  seq.grid_h = gh;

  const size_t n_dacs = seq.dacs.size();
  const size_t n_data = frame_count(j, "data");
  const size_t n_focus = frame_count(j, "focus");
  const bool has_quality_field = j.contains("quality") && j["quality"].is_array();
  const size_t n_quality = has_quality_field ? j["quality"].size() : kAbsent;

  if (n_data == kAbsent) add(out.issues, Severity::error, "缺必填欄位:data");
  if (n_focus == kAbsent)
    add(out.issues, Severity::error, "缺必填欄位:focus(SPEC-004 §3a 必填,Phase F 需要)");

  // 逐欄位幀數不一致 → 記 issue 並截斷至最小共同幀數(仍可瀏覽既有部分)。
  size_t n = n_dacs;
  const auto check_frames = [&](size_t nf, const char* name) {
    if (nf != kAbsent && nf != n_dacs) {
      add(out.issues, Severity::error, std::string(name) + " 幀數(" + std::to_string(nf) +
                                            ")與 dacs(" + std::to_string(n_dacs) +
                                            ")不一致,截斷至最小共同幀數");
      n = std::min(n, nf);
    }
  };
  check_frames(n_data, "data");
  check_frames(n_focus, "focus");
  check_frames(n_quality, "quality");

  seq.dacs.resize(n);
  seq.has_quality = has_quality_field;

  const size_t cells = static_cast<size_t>(gw) * static_cast<size_t>(gh);
  size_t quality_total = 0, quality_bad = 0;

  for (size_t f = 0; f < n; ++f) {
    seq.data.push_back(n_data != kAbsent ? parse_plane(j["data"][f], gw, gh, "data", f, out.issues)
                                         : std::vector<double>(cells, std::nan("")));
    seq.focus.push_back(n_focus != kAbsent
                            ? parse_plane(j["focus"][f], gw, gh, "focus", f, out.issues)
                            : std::vector<double>(cells, std::nan("")));
    if (has_quality_field) {
      auto qp = parse_plane(j["quality"][f], gw, gh, "quality", f, out.issues);
      for (double v : qp) {
        if (std::isnan(v)) continue;
        ++quality_total;
        if (v < 0.0 || v > 1.0) ++quality_bad;
      }
      seq.quality.push_back(std::move(qp));
    }
  }

  if (quality_bad > 0)
    add(out.issues, Severity::warn,
        "quality 有 " + std::to_string(quality_bad) + " / " + std::to_string(quality_total) +
            " 值超出 [0,1](SPEC-004 §3a.1 語意提案)");

  // 全幀皆 null 之 cell:可能是壞區/未實作區,列前幾例供追溯(避免細粒度時 issue 洗版)。
  if (!seq.data.empty()) {
    std::string examples;
    int n_all_null = 0;
    for (size_t idx = 0; idx < cells; ++idx) {
      bool all_null = true;
      for (size_t f = 0; f < n; ++f)
        if (!std::isnan(seq.data[f][idx])) { all_null = false; break; }
      if (!all_null) continue;
      if (n_all_null < 5) {
        const size_t r = idx / static_cast<size_t>(gw), c = idx % static_cast<size_t>(gw);
        if (!examples.empty()) examples += ", ";
        examples += "(r=" + std::to_string(r) + ",c=" + std::to_string(c) + ")";
      }
      ++n_all_null;
    }
    if (n_all_null > 0)
      add(out.issues, Severity::warn, "data 有 " + std::to_string(n_all_null) +
                                          " 個 cell 全幀皆 null,例如 " + examples);
  }

  out.ok = true;
  return out;
}

LoadResult load_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    LoadResult r;
    add(r.issues, Severity::error, "檔案無法開啟:" + path);
    return r;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return load(ss.str());
}

}  // namespace dcc::seqview

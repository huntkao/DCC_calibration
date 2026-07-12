#include "panels.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <imgui.h>
#include <implot.h>

#include "dcc_io/config.hpp"

namespace dcc::gui {

namespace {

// 狀態色(淡色主題下維持高對比)。
constexpr ImVec4 kPassCol{0.05f, 0.55f, 0.22f, 1.0f};
constexpr ImVec4 kFailCol{0.80f, 0.10f, 0.10f, 1.0f};
constexpr ImVec4 kWarnCol{0.72f, 0.48f, 0.00f, 1.0f};

bool slider_d(const char* label, double* v, float lo, float hi, const char* fmt = "%.2f") {
  float f = static_cast<float>(*v);
  if (ImGui::SliderFloat(label, &f, lo, hi, fmt)) { *v = static_cast<double>(f); return true; }
  return false;
}

// ── Sim 工作台 ──────────────────────────────────────────────────────────
void draw_sim_panel(GuiState& s) {
  ImGui::Begin("Sim 工作台");
  ImGui::TextWrapped("合成序列生成(dcc_sim)。參數變更後即時重生成並跑完整管線;"
                     "生成之序列與外部 SAD 模組同格式(SPEC-004 §3a)。");
  ImGui::Separator();

  bool ch = false;
  ch |= slider_d("雜訊 σ [raw px]", &s.spec.noise_sigma, 0.0f, 2.0f);
  ch |= slider_d("系統偏差 bias [raw px]", &s.spec.bias, -1.0f, 1.0f);
  int seed = static_cast<int>(s.spec.seed);
  if (ImGui::InputInt("seed", &seed)) { s.spec.seed = static_cast<unsigned>(seed < 0 ? 0 : seed); ch = true; }
  ch |= slider_d("合焦位置 [DAC]", &s.spec.focus_center, 180.0f, 660.0f, "%.0f");
  ch |= slider_d("中央 DCC 真值", &s.spec.center_dcc, 8.0f, 20.0f);
  ch |= slider_d("角落 DCC 真值", &s.spec.corner_dcc, 8.0f, 20.0f);
  ch |= ImGui::Checkbox("以 144×108 細粒度輸出(行使 D-5 聚合)", &s.fine_grid);
  ch |= ImGui::SliderInt("角落區 (5,7) null 幀數", &s.null_frames, 0, 5);
  ImGui::SameLine();
  ImGui::TextDisabled("(≥3 幀 → E-D03)");
  if (ch) s.dirty = true;

  ImGui::Separator();
  ImGui::Checkbox("參數變更自動重算", &s.auto_run);
  if (ImGui::Button("重新生成並執行")) s.regenerate_and_run();
  ImGui::SameLine();
  if (ImGui::Button("序列存檔")) {
    namespace fs = std::filesystem;
    fs::create_directories("data/disparity/SIM_GUI");
    std::ofstream("data/disparity/SIM_GUI/disp_seq.json") << s.last_seq_json;
    s.log_add(LogLevel::info, "序列已存:data/disparity/SIM_GUI/disp_seq.json");
  }
  if (s.dirty) { ImGui::SameLine(); ImGui::TextColored(kWarnCol, "● 結果已過期(stale)"); }
  ImGui::End();
}

// ── Config 面板 ─────────────────────────────────────────────────────────
void draw_config_panel(GuiState& s) {
  ImGui::Begin("Config");
  ImGui::TextDisabled("hash(載入時): %s", s.cfg.hash.c_str());
  bool ch = false;

  if (ImGui::CollapsingHeader("VCM / Sweep", ImGuiTreeNodeFlags_DefaultOpen)) {
    ch |= ImGui::InputInt("AF_CAL_INF", &s.cfg.vcm.af_cal_inf);
    ch |= ImGui::InputInt("AF_CAL_MACRO", &s.cfg.vcm.af_cal_macro);
    ch |= slider_d("far_margin", &s.cfg.sweep.far_margin, 0.0f, 0.2f, "%.3f");
    ch |= slider_d("near_margin", &s.cfg.sweep.near_margin, 0.0f, 0.2f, "%.3f");
  }
  if (ImGui::CollapsingHeader("判定 / 品質", ImGuiTreeNodeFlags_DefaultOpen)) {
    ch |= slider_d("tolerance", &s.cfg.tolerance, 0.05f, 0.30f, "%.3f");
    ch |= ImGui::SliderInt("min_valid_samples", &s.cfg.min_valid_samples, 6, 10);
    ch |= slider_d("r2_warn", &s.cfg.r2_warn, 0.90f, 1.00f, "%.3f");
    ch |= slider_d("smooth_limit", &s.cfg.smooth_limit, 0.05f, 0.50f, "%.3f");
  }
  if (ImGui::CollapsingHeader("打包 / 聚合")) {
    ch |= ImGui::SliderInt("q_format", &s.cfg.q_format, 4, 8);
    const char* units[] = {"raw_pixel", "pd_image_grid"};
    int ou = (s.cfg.output_disparity_unit == "pd_image_grid") ? 1 : 0;
    if (ImGui::Combo("output_disparity_unit", &ou, units, 2)) {
      s.cfg.output_disparity_unit = units[ou];
      ch = true;
    }
    const char* methods[] = {"median", "weighted_mean"};
    int m = (s.cfg.agg_method == dcc::aggregate::Method::weighted_mean) ? 1 : 0;
    if (ImGui::Combo("aggregation.method", &m, methods, 2)) {
      s.cfg.agg_method = m ? dcc::aggregate::Method::weighted_mean
                           : dcc::aggregate::Method::median;
      ch = true;
    }
    ch |= slider_d("min_valid_ratio", &s.cfg.min_valid_ratio, 0.0f, 1.0f, "%.2f");
  }
  if (ImGui::Button("重設為預設 config")) {
    s.cfg = dcc::io::load_config(dcc::io::default_config_json());
    ch = true;
    s.log_add(LogLevel::info, "config 已重設為預設值");
  }
  if (ch) s.dirty = true;
  ImGui::End();
}

// ── 結果總覽 ────────────────────────────────────────────────────────────
void draw_overview(GuiState& s) {
  ImGui::Begin("結果總覽");
  if (!s.error_code.empty()) {
    ImGui::TextColored(kFailCol, "中止:%s", s.error_code.c_str());
    ImGui::TextWrapped("%s", s.error_msg.c_str());
  } else if (s.has_result) {
    const auto& r = s.result;
    ImGui::Text("模組:%s", r.module_id.c_str());
    ImGui::SameLine();
    ImGui::TextColored(r.pass ? kPassCol : kFailCol, "  %s", r.pass ? "PASS" : "FAIL");
    ImGui::Text("sweep:%d → %d(span %.0f DAC,%d 點)", r.dacs.front(), r.dacs.back(), r.span,
                static_cast<int>(r.dacs.size()));
    const size_t center = static_cast<size_t>(3) * 8 + 4;
    ImGui::Text("中央區 DCC:%.4f DAC/raw_px(pd_grid:%.2f)", r.regions[center].dcc_raw_px,
                r.regions[center].dcc_pd_grid);
    ImGui::Text("block:%zu bytes", r.block.size());
    ImGui::Separator();
    ImGui::Text("最差三區:");
    for (const auto& w : r.worst) {
      const int wr = static_cast<int>(w.index / 8), wc = static_cast<int>(w.index % 8);
      ImGui::BulletText("(r=%d, c=%d) err=%.4f", wr, wc, w.err);
      if (ImGui::IsItemClicked()) { s.sel_r = wr; s.sel_c = wc; }
    }
    if (!r.warnings.empty() &&
        ImGui::CollapsingHeader(("警告(" + std::to_string(r.warnings.size()) + ")").c_str()))
      for (const auto& w : r.warnings) ImGui::TextColored(kWarnCol, "%s", w.c_str());
  } else {
    ImGui::TextDisabled("尚無結果");
  }
  ImGui::End();
}

// ── Heatmap(DCC / err;點擊選區)────────────────────────────────────────
void heatmap_plot(GuiState& s, const char* title, const std::vector<double>& vals,
                  double vmin, double vmax, const char* fmt) {
  if (ImPlot::BeginPlot(title, ImVec2(-1, 260),
                        ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
    ImPlot::SetupAxes(nullptr, nullptr,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock);
    ImPlot::SetupAxesLimits(0, 8, 0, 6, ImGuiCond_Always);
    ImPlot::PlotHeatmap(title, vals.data(), 6, 8, vmin, vmax, fmt, ImPlotPoint(0, 0),
                        ImPlotPoint(8, 6));
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const ImPlotPoint p = ImPlot::GetPlotMousePos();
      const int col = static_cast<int>(std::floor(p.x));
      const int row = 5 - static_cast<int>(std::floor(p.y));  // 畫面上排 = r0
      if (row >= 0 && row < 6 && col >= 0 && col < 8) { s.sel_r = row; s.sel_c = col; }
    }
    ImPlot::EndPlot();
  }
}

void draw_maps(GuiState& s) {
  ImGui::Begin("DCC / 誤差 map");
  if (!s.has_result) { ImGui::TextDisabled("尚無結果"); ImGui::End(); return; }

  // row-major:畫面上排須為 r=0 → heatmap 資料列反轉。
  std::vector<double> dccv(48), errv(48);
  for (int r = 0; r < 6; ++r)
    for (int c = 0; c < 8; ++c) {
      const size_t src = static_cast<size_t>(r) * 8 + static_cast<size_t>(c);
      const size_t dst = static_cast<size_t>(5 - r) * 8 + static_cast<size_t>(c);
      dccv[dst] = s.result.regions[src].dcc_raw_px;
      errv[dst] = s.result.regions[src].err;
    }
  double dmin = dccv[0], dmax = dccv[0];
  for (double v : dccv) { dmin = std::min(dmin, v); dmax = std::max(dmax, v); }

  ImPlot::PushColormap(ImPlotColormap_Viridis);
  heatmap_plot(s, "DCC [DAC/raw_px]", dccv, dmin, dmax, "%.2f");
  ImPlot::PopColormap();
  ImPlot::PushColormap(ImPlotColormap_Hot);
  heatmap_plot(s, "err(tolerance 檢核)", errv, 0.0, s.cfg.tolerance, "%.3f");
  ImPlot::PopColormap();
  ImGui::Text("點擊格子選擇區域;目前:(r=%d, c=%d)", s.sel_r, s.sel_c);
  ImGui::End();
}

// ── 區域檢視(回歸線 + focus 曲線)───────────────────────────────────────
void draw_region_detail(GuiState& s) {
  ImGui::Begin("區域檢視");
  if (!s.has_result) { ImGui::TextDisabled("尚無結果"); ImGui::End(); return; }

  const auto& res = s.result;
  const size_t ri = static_cast<size_t>(s.sel_r) * 8 + static_cast<size_t>(s.sel_c);
  const auto& reg = res.regions[ri];
  ImGui::Text("區 (r=%d, c=%d):DCC=%.4f  b=%.2f  r²=%.5f  peak=%.2f  err=%.4f  %s", s.sel_r,
              s.sel_c, reg.dcc_raw_px, reg.intercept, reg.r2, reg.focus_peak, reg.err,
              reg.pass ? "PASS" : "FAIL");

  const size_t n = res.seq.disp.size();
  std::vector<double> xs, ys, fx(n), fy(n);
  for (size_t f = 0; f < n; ++f) {
    fx[f] = static_cast<double>(res.dacs[f]);
    fy[f] = res.seq.focus[f][ri];
    const double d = res.seq.disp[f][ri];
    if (!std::isnan(d)) { xs.push_back(d); ys.push_back(static_cast<double>(res.dacs[f])); }
  }

  if (ImPlot::BeginPlot("回歸:DAC = k·disp + b", ImVec2(-1, 240))) {
    ImPlot::SetupAxes("disparity [raw px]", "DAC");
    ImPlot::PlotScatter("樣本", xs.data(), ys.data(), static_cast<int>(xs.size()));
    if (xs.size() >= 2) {
      const double x0 = *std::min_element(xs.begin(), xs.end());
      const double x1 = *std::max_element(xs.begin(), xs.end());
      const double lx[2] = {x0, x1};
      const double ly[2] = {reg.dcc_raw_px * x0 + reg.intercept,
                            reg.dcc_raw_px * x1 + reg.intercept};
      ImPlot::PlotLine("擬合", lx, ly, 2);
      const double bx = 0.0, by = reg.intercept;
      ImPlot::PlotScatter("截距 b", &bx, &by, 1);
    }
    ImPlot::EndPlot();
  }
  if (ImPlot::BeginPlot("focus 曲線與峰值", ImVec2(-1, 240))) {
    ImPlot::SetupAxes("DAC", "focus value");
    ImPlot::PlotLine("fv", fx.data(), fy.data(), static_cast<int>(n));
    const double peak = reg.focus_peak;
    ImPlot::PlotInfLines("peak", &peak, 1);
    const double step = res.span / static_cast<double>(n - 1);
    const double bounds[2] = {static_cast<double>(res.dacs.front()) + step,
                              static_cast<double>(res.dacs.back()) - step};
    ImPlot::PlotInfLines("E-F01 邊界", bounds, 2);
    ImPlot::EndPlot();
  }
  ImGui::End();
}

// ── Log 主控台 ──────────────────────────────────────────────────────────
void draw_log(GuiState& s) {
  ImGui::Begin("Log 主控台");
  static char filter[128] = "";
  static bool show_info = true, show_warn = true, show_err = true;
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##filter", "過濾…", filter, sizeof(filter));
  ImGui::SameLine(); ImGui::Checkbox("info", &show_info);
  ImGui::SameLine(); ImGui::Checkbox("warn", &show_warn);
  ImGui::SameLine(); ImGui::Checkbox("error", &show_err);
  ImGui::SameLine(); ImGui::Checkbox("自動捲動", &s.log_autoscroll);
  ImGui::SameLine();
  if (ImGui::Button("清除")) s.log.clear();

  ImGui::BeginChild("logscroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
  for (const auto& e : s.log) {
    if (e.level == LogLevel::info && !show_info) continue;
    if (e.level == LogLevel::warn && !show_warn) continue;
    if (e.level == LogLevel::error && !show_err) continue;
    if (filter[0] && e.msg.find(filter) == std::string::npos) continue;
    if (e.level == LogLevel::error) ImGui::TextColored(kFailCol, "%s", e.msg.c_str());
    else if (e.level == LogLevel::warn) ImGui::TextColored(kWarnCol, "%s", e.msg.c_str());
    else ImGui::TextUnformatted(e.msg.c_str());
  }
  if (s.log_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace

void draw_all(GuiState& s) {
  draw_sim_panel(s);
  draw_config_panel(s);
  draw_overview(s);
  draw_maps(s);
  draw_region_detail(s);
  draw_log(s);
}

}  // namespace dcc::gui

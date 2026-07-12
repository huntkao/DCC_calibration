#include "panels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <GLFW/glfw3.h>  // GL 紋理上傳(RAW 檢視)
#include <imgui.h>
#include <implot.h>

#include "dcc_app/session.hpp"
#include "dcc_io/config.hpp"
#include "dcc_io/raw_reader.hpp"

namespace dcc::gui {

namespace {

// 狀態色(淡色主題下維持高對比)。
constexpr ImVec4 kPassCol{0.05f, 0.55f, 0.22f, 1.0f};
constexpr ImVec4 kFailCol{0.80f, 0.10f, 0.10f, 1.0f};
constexpr ImVec4 kWarnCol{0.72f, 0.48f, 0.00f, 1.0f};

// 存檔檔名:參數後綴(σ/bias/seed/合焦/粒度/null)+ 時間戳,提高辨識度。
std::string seq_filename(const GuiState& s) {
  char ts[32];
  const std::time_t t = std::time(nullptr);
  std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", std::localtime(&t));
  char buf[256];
  std::snprintf(buf, sizeof(buf), "disp_seq_s%.2f_b%.2f_sd%u_fc%.0f_%dx%d%s_%s.json",
                s.spec.noise_sigma, s.spec.bias, s.spec.seed, s.spec.focus_center,
                s.spec.grid_w, s.spec.grid_h,
                s.null_frames > 0 ? ("_null" + std::to_string(s.null_frames)).c_str() : "", ts);
  return buf;
}

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
  ImGui::PushItemWidth(190.0f);

  bool ch = false;
  ch |= slider_d("雜訊 σ [raw px]", &s.spec.noise_sigma, 0.0f, 2.0f);
  ch |= slider_d("系統偏差 bias [raw px]", &s.spec.bias, -1.0f, 1.0f);
  int seed = static_cast<int>(s.spec.seed);
  if (ImGui::InputInt("seed", &seed)) { s.spec.seed = static_cast<unsigned>(seed < 0 ? 0 : seed); ch = true; }
  ch |= slider_d("合焦位置 [DAC]", &s.spec.focus_center, 180.0f, 660.0f, "%.0f");
  ch |= slider_d("中央 DCC 真值", &s.spec.center_dcc, 8.0f, 20.0f);
  ch |= slider_d("角落 DCC 真值", &s.spec.corner_dcc, 8.0f, 20.0f);
  ch |= slider_d("非線性 nl(視差二階項)", &s.spec.nonlinearity, 0.0f, 0.2f, "%.3f");
  ch |= slider_d("focus 峰值偏移 [DAC]", &s.spec.focus_peak_offset, -120.0f, 120.0f, "%.0f");
  ImGui::SameLine();
  ImGui::TextDisabled("(err 演練:±96 → err 0.20 踩線)");
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
    const std::string path = "data/disparity/SIM_GUI/" + seq_filename(s);
    std::ofstream(path) << dcc::sim::pretty(s.last_seq_json);
    s.log_add(LogLevel::info, "序列已存:" + path);
  }
  if (s.dirty) { ImGui::SameLine(); ImGui::TextColored(kWarnCol, "● 結果已過期(stale)"); }
  ImGui::PopItemWidth();
  ImGui::End();
}

// ── Config 面板 ─────────────────────────────────────────────────────────
void draw_config_panel(GuiState& s) {
  ImGui::Begin("Config");
  ImGui::TextDisabled("hash(載入時): %s", s.cfg.hash.c_str());
  ImGui::PushItemWidth(190.0f);
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
  ImGui::PopItemWidth();
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

// ── Heatmap(DCC / err;點擊選區;fail_mask = 超差區黑框)────────────────
void heatmap_plot(GuiState& s, const char* title, const std::vector<double>& vals,
                  double vmin, double vmax, const char* fmt,
                  const std::vector<char>* fail_mask = nullptr) {
  if (ImPlot::BeginPlot(title, ImVec2(-1, 260),
                        ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
    ImPlot::SetupAxes(nullptr, nullptr,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock);
    ImPlot::SetupAxesLimits(0, 8, 0, 6, ImGuiCond_Always);
    ImPlot::PlotHeatmap(title, vals.data(), 6, 8, vmin, vmax, fmt, ImPlotPoint(0, 0),
                        ImPlotPoint(8, 6));

    if (fail_mask) {  // 超過 tolerance 之區:紅色滿版 + 白字重繪數值(硬性 FAIL 標示)
      ImPlot::PushPlotClipRect();
      auto* dl = ImPlot::GetPlotDrawList();
      for (size_t i = 0; i < fail_mask->size(); ++i) {
        if (!(*fail_mask)[i]) continue;
        const int r = static_cast<int>(i) / 8, c = static_cast<int>(i) % 8;
        const ImVec2 fa = ImPlot::PlotToPixels(ImPlotPoint(c, 5 - r));
        const ImVec2 fb = ImPlot::PlotToPixels(ImPlotPoint(c + 1, 6 - r));
        dl->AddRectFilled(fa, fb, IM_COL32(198, 28, 28, 255));
        dl->AddRect(fa, fb, IM_COL32(110, 0, 0, 255), 0.0f, 0, 2.0f);
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, vals[i]);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2((fa.x + fb.x - ts.x) * 0.5f, (fa.y + fb.y - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), buf);
      }
      ImPlot::PopPlotClipRect();
    }
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const ImPlotPoint p = ImPlot::GetPlotMousePos();
      const int col = static_cast<int>(std::floor(p.x));
      const int row = 5 - static_cast<int>(std::floor(p.y));  // 畫面上排 = r0
      if (row >= 0 && row < 6 && col >= 0 && col < 8) { s.sel_r = row; s.sel_c = col; }
    }
    // 選區高亮框。
    ImPlot::PushPlotClipRect();
    const ImVec2 a = ImPlot::PlotToPixels(ImPlotPoint(s.sel_c, 5 - s.sel_r));
    const ImVec2 b = ImPlot::PlotToPixels(ImPlotPoint(s.sel_c + 1, 6 - s.sel_r));
    ImPlot::GetPlotDrawList()->AddRect(a, b, IM_COL32(255, 70, 0, 255), 0.0f, 0, 2.5f);
    ImPlot::PopPlotClipRect();
    ImPlot::EndPlot();
  }
}

void draw_maps(GuiState& s) {
  ImGui::Begin("DCC / 誤差 map");
  if (!s.has_result) { ImGui::TextDisabled("尚無結果"); ImGui::End(); return; }

  // ImPlot heatmap 的第一列即畫在最上方 → 直接以 row-major 餵入,
  // r=0 顯示在上排,與點擊換算/高亮框/區域檢視一致(勿再翻轉!)。
  std::vector<double> dccv(48), errv(48);
  for (size_t i = 0; i < 48; ++i) {
    dccv[i] = s.result.regions[i].dcc_raw_px;
    errv[i] = s.result.regions[i].err;
  }
  double dmin = dccv[0], dmax = dccv[0];
  for (double v : dccv) { dmin = std::min(dmin, v); dmax = std::max(dmax, v); }

  ImPlot::PushColormap(ImPlotColormap_Viridis);
  heatmap_plot(s, "DCC [DAC/raw_px]", dccv, dmin, dmax, "%.2f");
  ImPlot::PopColormap();
  std::vector<char> fail(48);
  int n_fail = 0;
  for (size_t i = 0; i < 48; ++i) {
    fail[i] = errv[i] >= s.cfg.tolerance ? 1 : 0;
    n_fail += fail[i];
  }
  // 嚴重度 colormap:深綠(安全)→ 淺綠 → 黃(逼近 tolerance);超差由紅色覆蓋。
  static const ImPlotColormap severity = [] {
    static const ImVec4 g[] = {{0.01f, 0.32f, 0.12f, 1.0f}, {0.09f, 0.47f, 0.20f, 1.0f},
                               {0.30f, 0.64f, 0.33f, 1.0f}, {0.58f, 0.80f, 0.45f, 1.0f},
                               {0.84f, 0.90f, 0.42f, 1.0f}, {0.96f, 0.84f, 0.22f, 1.0f}};
    return ImPlot::AddColormap("DccErrSeverity", g, 6, false);
  }();
  ImPlot::PushColormap(severity);
  heatmap_plot(s, "err(tolerance 檢核)", errv, 0.0, s.cfg.tolerance, "%.3f", &fail);
  ImPlot::PopColormap();
  ImGui::Text("點擊格子選擇區域;目前:(r=%d, c=%d)。紅色 = err ≥ tolerance(%d 區)",
              s.sel_r, s.sel_c, n_fail);
  ImGui::End();
}

// ── 區域檢視(回歸線 + focus 曲線)───────────────────────────────────────
void draw_region_detail(GuiState& s) {
  ImGui::Begin("區域檢視");
  if (!s.has_result) { ImGui::TextDisabled("尚無結果"); ImGui::End(); return; }

  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("r", &s.sel_r, 0, 5);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("c", &s.sel_c, 0, 7);

  const auto& res = s.result;
  const size_t ri = static_cast<size_t>(s.sel_r) * 8 + static_cast<size_t>(s.sel_c);
  const auto& reg = res.regions[ri];
  ImGui::Text("區 (r=%d, c=%d):DCC(k)=%.4f  截距 b=%.2f  r²=%.5f  focus peak=%.2f",
              s.sel_r, s.sel_c, reg.dcc_raw_px, reg.intercept, reg.r2, reg.focus_peak);
  ImGui::Text("err = |b − peak| / span = |%.2f − %.2f| / %.0f = %.4f  → %s", reg.intercept,
              reg.focus_peak, res.span, reg.err, reg.pass ? "PASS" : "FAIL");
  ImGui::TextDisabled("(k 即 DCC 斜率;r² 為迴歸擬合品質;map 上顯示同值,位數較少)");

  const size_t n = res.seq.disp.size();
  std::vector<double> xs, ys, fx(n), fy(n);
  for (size_t f = 0; f < n; ++f) {
    fx[f] = static_cast<double>(res.dacs[f]);
    fy[f] = res.seq.focus[f][ri];
    const double d = res.seq.disp[f][ri];
    if (!std::isnan(d)) { xs.push_back(d); ys.push_back(static_cast<double>(res.dacs[f])); }
  }

  if (ImPlot::BeginPlot("回歸:DAC = k·disp + b", ImVec2(-1, 240))) {
    ImPlot::SetupAxes("disparity [raw px]", "DAC", ImPlotAxisFlags_AutoFit,
                      ImPlotAxisFlags_AutoFit);
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
    ImPlot::SetupAxes("DAC", "focus value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
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

// ── RAW 檢視(FR-18:唯讀底圖;縮放平移 + DN 探針 + PD/區格疊層)─────────
// 假設模組之 PD offsets(與 config 範例一致;M2 對接後改由 config 提供)。
constexpr int kPdL[8][2] = {{4, 3}, {20, 3}, {4, 11}, {20, 11}, {4, 19}, {20, 19}, {4, 27}, {20, 27}};
constexpr int kPdR[8][2] = {{8, 3}, {24, 3}, {8, 11}, {24, 11}, {8, 19}, {24, 19}, {8, 27}, {24, 27}};

int pd_type(int x, int y) {  // 0=一般、1=L、2=R
  const int mx = x % 32, my = y % 32;
  for (const auto& p : kPdL)
    if (p[0] == mx && p[1] == my) return 1;
  for (const auto& p : kPdR)
    if (p[0] == mx && p[1] == my) return 2;
  return 0;
}

void upload_raw_texture(GuiState& s) {
  if (!s.raw_loaded) return;
  const int w = s.raw.width, h = s.raw.height;
  const double lo = s.raw_lo, hi = std::max(s.raw_lo + 1, s.raw_hi);
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  for (size_t i = 0; i < s.raw.pixels.size(); ++i) {
    double v = (s.raw.pixels[i] - lo) / (hi - lo);
    v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    const auto g = static_cast<unsigned char>(v * 255.0 + 0.5);
    rgba[i * 4 + 0] = g; rgba[i * 4 + 1] = g; rgba[i * 4 + 2] = g; rgba[i * 4 + 3] = 255;
  }
  if (s.raw_tex == 0) glGenTextures(1, &s.raw_tex);
  glBindTexture(GL_TEXTURE_2D, s.raw_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  // 高倍率下像素銳利
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  s.raw_tex_stale = false;
}

void draw_raw_view(GuiState& s) {
  ImGui::Begin("RAW 檢視");
  if (ImGui::Button("生成示範 RAW")) {
    dcc::sim::RawSpec rs;
    rs.width = s.cfg.sensor_width;
    rs.height = s.cfg.sensor_height;
    s.raw.width = rs.width;
    s.raw.height = rs.height;
    s.raw.pixels = dcc::sim::generate_raw(rs);
    s.raw_loaded = true;
    s.raw_tex_stale = true;
    s.log_add(LogLevel::info, "示範 RAW 已生成(" + std::to_string(rs.width) + "×" +
                                  std::to_string(rs.height) + ",10-bit)");
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(320);
  ImGui::InputText("##rawpath", s.raw_path, sizeof(s.raw_path));
  ImGui::SameLine();
  if (ImGui::Button("載入 RAW")) {
    auto img = dcc::io::load_raw(s.raw_path, s.cfg.sensor_width, s.cfg.sensor_height);
    if (img) {
      s.raw = std::move(*img);
      s.raw_loaded = true;
      s.raw_tex_stale = true;
      s.log_add(LogLevel::info, std::string("RAW 已載入:") + s.raw_path);
    } else {
      s.log_add(LogLevel::warn, std::string("RAW 載入失敗(缺檔或尺寸不符),降級顯示:") +
                                    s.raw_path);
    }
  }

  // ── RAW 存檔(含覆寫確認)──────────────────────────────────────────
  static char save_path[256] = "data/raw/SIM_GUI/frame_demo.raw";
  const auto do_save = [&]() {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(save_path).parent_path());
    std::ofstream f(save_path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(s.raw.pixels.data()),
            static_cast<std::streamsize>(s.raw.pixels.size() * 2));
    std::snprintf(s.raw_path, sizeof(s.raw_path), "%s", save_path);  // 方便回讀驗證
    s.log_add(LogLevel::info, std::string("RAW 已存:") + save_path + "(" +
                                  std::to_string(s.raw.pixels.size() * 2) + " bytes)");
  };
  ImGui::SetNextItemWidth(320);
  ImGui::InputText("##savepath", save_path, sizeof(save_path));
  ImGui::SameLine();
  if (ImGui::Button("RAW 存檔")) {
    if (!s.raw_loaded) {
      s.log_add(LogLevel::warn, "尚無 RAW 內容可存");
    } else if (std::filesystem::exists(save_path)) {
      ImGui::OpenPopup("覆寫確認");
    } else {
      do_save();
    }
  }
  if (ImGui::BeginPopupModal("覆寫確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("檔案已存在:\n%s\n\n確定覆寫?", save_path);
    ImGui::Separator();
    if (ImGui::Button("覆寫", ImVec2(120, 0))) { do_save(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  int range[2] = {s.raw_lo, s.raw_hi};
  ImGui::SetNextItemWidth(190);
  if (ImGui::DragIntRange2("對比拉伸 [DN]", &range[0], &range[1], 2, 0, 1023)) {
    s.raw_lo = range[0]; s.raw_hi = range[1]; s.raw_tex_stale = true;
  }
  ImGui::SameLine(); ImGui::Checkbox("區格線", &s.raw_show_grid);
  ImGui::SameLine(); ImGui::Checkbox("PD 疊層", &s.raw_show_pd);

  if (!s.raw_loaded) {
    ImGui::TextDisabled("尚未載入 RAW(可按「生成示範 RAW」)");
    ImGui::End();
    return;
  }
  if (s.raw_tex_stale) upload_raw_texture(s);

  const double W = s.raw.width, H = s.raw.height;
  if (ImPlot::BeginPlot("##rawplot", ImVec2(-1, -1), ImPlotFlags_Equal)) {
    ImPlot::SetupAxis(ImAxis_X1, "x [px]");
    ImPlot::SetupAxis(ImAxis_Y1, "y [px]", ImPlotAxisFlags_Invert);  // row0 在上
    ImPlot::SetupAxesLimits(0, W, 0, H, ImGuiCond_Once);
    ImPlot::PlotImage("##raw", static_cast<ImTextureID>(static_cast<intptr_t>(s.raw_tex)),
                      ImPlotPoint(0, 0), ImPlotPoint(W, H));

    if (s.raw_show_grid) {  // 8×6 區格線
      double xs[7], ys[5];
      for (int k = 1; k < 8; ++k) xs[k - 1] = W * k / 8.0;
      for (int k = 1; k < 6; ++k) ys[k - 1] = H * k / 6.0;
      ImPlot::PlotInfLines("##gx", xs, 7);
      ImPlot::PlotInfLines("##gy", ys, 5, ImPlotInfLinesFlags_Horizontal);
    }

    // 高倍率時疊 PD 像素位置(視野 < 700 px 才畫,避免點數爆炸)。
    const ImPlotRect lim = ImPlot::GetPlotLimits();
    if (s.raw_show_pd && (lim.X.Max - lim.X.Min) < 700.0 && (lim.Y.Max - lim.Y.Min) < 700.0) {
      std::vector<double> lx, ly, rx, ry;
      const int x0 = std::max(0, static_cast<int>(lim.X.Min) / 32 * 32);
      const int y0 = std::max(0, static_cast<int>(lim.Y.Min) / 32 * 32);
      for (int by = y0; by < lim.Y.Max && by < H; by += 32)
        for (int bx = x0; bx < lim.X.Max && bx < W; bx += 32)
          for (int i = 0; i < 8; ++i) {
            lx.push_back(bx + kPdL[i][0] + 0.5); ly.push_back(by + kPdL[i][1] + 0.5);
            rx.push_back(bx + kPdR[i][0] + 0.5); ry.push_back(by + kPdR[i][1] + 0.5);
          }
      // 同步設定 item 主色(legend 取 item 色)與 marker 色,確保兩者一致。
      const ImVec4 col_l(0.15f, 0.45f, 0.95f, 0.85f), col_r(0.90f, 0.20f, 0.20f, 0.85f);
      ImPlot::SetNextLineStyle(col_l);
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4, col_l, IMPLOT_AUTO, col_l);
      ImPlot::PlotScatter("PD L", lx.data(), ly.data(), static_cast<int>(lx.size()));
      ImPlot::SetNextLineStyle(col_r);
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4, col_r, IMPLOT_AUTO, col_r);
      ImPlot::PlotScatter("PD R", rx.data(), ry.data(), static_cast<int>(rx.size()));
    }

    if (ImPlot::IsPlotHovered()) {  // DN 探針
      const ImPlotPoint p = ImPlot::GetPlotMousePos();
      const int px = static_cast<int>(p.x), py = static_cast<int>(p.y);
      if (px >= 0 && px < s.raw.width && py >= 0 && py < s.raw.height) {
        const std::uint16_t dn =
            s.raw.pixels[static_cast<size_t>(py) * static_cast<size_t>(s.raw.width) +
                         static_cast<size_t>(px)];
        const int t = pd_type(px, py);

        // 高倍率(單像素 ≥ 6 螢幕 px)時 snap 到像素並描框。
        const ImVec2 pa = ImPlot::PlotToPixels(ImPlotPoint(px, py));
        const ImVec2 pb = ImPlot::PlotToPixels(ImPlotPoint(px + 1, py + 1));
        if (std::fabs(pb.x - pa.x) >= 6.0f) {
          ImPlot::PushPlotClipRect();
          ImPlot::GetPlotDrawList()->AddRect(pa, pb, IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.0f);
          ImPlot::PopPlotClipRect();
        }
        ImGui::BeginTooltip();
        ImGui::Text("(%d, %d)  DN=%u%s", px, py, dn,
                    t == 1 ? "  [PD L]" : (t == 2 ? "  [PD R]" : ""));
        ImGui::Text("區 (r=%d, c=%d)", py / (s.raw.height / 6), px / (s.raw.width / 8));
        ImGui::EndTooltip();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {  // 雙擊選區
          s.sel_r = py / (s.raw.height / 6);
          s.sel_c = px / (s.raw.width / 8);
        }
      }
    }
    ImPlot::EndPlot();
  }
  ImGui::End();
}

// ── 靈敏度掃描(開放問題 #3:合焦偏移 vs DCC/err)────────────────────────
void draw_scan(GuiState& s) {
  ImGui::Begin("靈敏度掃描");
  ImGui::TextWrapped("開放問題 #3:chart 距離(→ 合焦位置)偏移對 DCC 的靈敏度。"
                     "沿用 Sim 工作台目前參數(σ/bias/nl/seed),僅平移合焦位置。"
                     "注意:nl=0(理想線性)時 DCC 理論上不受偏移影響,"
                     "請先於 Sim 工作台設定非線性 nl。");
  ImGui::PushItemWidth(190.0f);
  float range = static_cast<float>(s.scan_range);
  if (ImGui::SliderFloat("掃描範圍 ±[DAC]", &range, 10.0f, 200.0f, "%.0f"))
    s.scan_range = static_cast<double>(range);
  ImGui::SliderInt("掃描點數", &s.scan_steps, 9, 81);
  ImGui::PopItemWidth();
  if (ImGui::Button("執行掃描")) s.run_scan();

  if (!s.scan.empty()) {
    std::vector<double> xs, dpct, merr;
    int aborted = 0;
    double worst_dpct = 0.0;
    for (const auto& p : s.scan) {
      if (!p.error.empty()) { ++aborted; continue; }
      xs.push_back(p.offset);
      dpct.push_back(p.delta_pct);
      merr.push_back(p.max_err);
      worst_dpct = std::max(worst_dpct, std::fabs(p.delta_pct));
    }
    ImGui::Text("有效 %d / %d 點(中止 %d);|ΔDCC| 最大 %.2f%%", static_cast<int>(xs.size()),
                static_cast<int>(s.scan.size()), aborted, worst_dpct);

    if (ImPlot::BeginPlot("ΔDCC vs 合焦偏移", ImVec2(-1, 220))) {
      ImPlot::SetupAxes("合焦偏移 [DAC]", "中央 DCC 變化 [%]", ImPlotAxisFlags_AutoFit,
                        ImPlotAxisFlags_AutoFit);
      ImPlot::PlotLine("ΔDCC%", xs.data(), dpct.data(), static_cast<int>(xs.size()));
      const double one[2] = {1.0, -1.0};  // ±1% 參考線
      ImPlot::PlotInfLines("±1%", one, 2, ImPlotInfLinesFlags_Horizontal);
      ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("最大區域 err vs 合焦偏移", ImVec2(-1, 220))) {
      ImPlot::SetupAxes("合焦偏移 [DAC]", "max err", ImPlotAxisFlags_AutoFit,
                        ImPlotAxisFlags_AutoFit);
      ImPlot::PlotLine("max err", xs.data(), merr.data(), static_cast<int>(xs.size()));
      const double tol = s.cfg.tolerance;
      ImPlot::PlotInfLines("tolerance", &tol, 1, ImPlotInfLinesFlags_Horizontal);
      ImPlot::EndPlot();
    }
  } else {
    ImGui::TextDisabled("尚未執行掃描");
  }
  ImGui::End();
}

// ── 報告檢視器(report.json 快照 + 落盤)─────────────────────────────────
void draw_report(GuiState& s) {
  ImGui::Begin("報告");
  if (s.report_json.empty()) {
    ImGui::TextDisabled("尚無報告(管線完成後自動生成)");
    ImGui::End();
    return;
  }
  static char out_dir[256] = "data/output/SIM0001";
  ImGui::SetNextItemWidth(280);
  ImGui::InputText("##outdir", out_dir, sizeof(out_dir));
  ImGui::SameLine();
  const auto do_write = [&]() {
    namespace fs = std::filesystem;
    fs::create_directories(out_dir);
    std::ofstream(std::string(out_dir) + "/report.json") << s.report_json;
    std::ofstream(std::string(out_dir) + "/report.md")
        << dcc::app::build_report_md(s.cfg, s.result);
    std::ofstream blk(std::string(out_dir) + "/block.bin", std::ios::binary);
    blk.write(reinterpret_cast<const char*>(s.result.block.data()),
              static_cast<std::streamsize>(s.result.block.size()));
    s.log_add(LogLevel::info, std::string("報告已落盤:") + out_dir +
                                  "/report.{json,md} + block.bin");
  };
  if (ImGui::Button("報告落盤")) {
    if (std::filesystem::exists(std::string(out_dir) + "/report.json"))
      ImGui::OpenPopup("報告覆寫確認");
    else do_write();
  }
  if (ImGui::BeginPopupModal("報告覆寫確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("目錄已有 report.json:\n%s\n\n確定覆寫?", out_dir);
    ImGui::Separator();
    if (ImGui::Button("覆寫", ImVec2(120, 0))) { do_write(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu bytes)", s.report_json.size());

  ImGui::BeginChild("reportscroll", ImVec2(0, 0), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::TextUnformatted(s.report_json.c_str());
  ImGui::EndChild();
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
  draw_raw_view(s);
  draw_scan(s);
  draw_report(s);
  draw_log(s);
}

}  // namespace dcc::gui

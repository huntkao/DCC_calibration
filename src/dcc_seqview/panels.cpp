#include "panels.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "dcc_gui_common/file_dialog.hpp"

namespace dcc::seqview {

namespace {

// 狀態色(與 dcc_gui 相同色值,保持視覺一致;獨立複製以維持零耦合)。
constexpr ImVec4 kErrCol{0.80f, 0.10f, 0.10f, 1.0f};
constexpr ImVec4 kWarnCol{0.72f, 0.48f, 0.00f, 1.0f};
constexpr ImVec4 kOkCol{0.05f, 0.55f, 0.22f, 1.0f};
constexpr ImVec4 kSampleBlue{0.15f, 0.45f, 0.95f, 1.0f};

void draw_file_panel(ViewerState& s) {
  ImGui::Begin("開啟檔案");
  ImGui::TextWrapped(
      "獨立檢視外部 SAD 模組(或本工具 Sim 工作台)自產之 disp_seq.json(SPEC-004 §3a)。"
      "顯示原始粒度,不做 D-5 聚合/單位轉換/校正計算——純粹瀏覽與結構自檢。");
  ImGui::InputText("路徑", s.path, sizeof(s.path));
  ImGui::SameLine();
  if (ImGui::Button("開啟")) {
    if (std::strlen(s.path) == 0) {
      // 路徑欄位為空 → 開原生檔案瀏覽視窗讓使用者選檔;取消則不動作。
      const std::string picked = dcc::guicommon::open_file_dialog(
          "選擇 disp_seq.json", "", {"*.json"}, "disp_seq JSON");
      if (!picked.empty()) {
        std::strncpy(s.path, picked.c_str(), sizeof(s.path) - 1);
        s.path[sizeof(s.path) - 1] = '\0';
        s.open(picked);
      }
    } else {
      s.open(s.path);
    }
  }

  if (!s.loaded) { ImGui::End(); return; }
  if (!s.result.ok) {
    ImGui::TextColored(kErrCol, "載入失敗,詳見「診斷」面板");
    ImGui::End();
    return;
  }

  const auto& seq = s.result.seq;
  ImGui::Separator();
  ImGui::Text("module_id:%s", seq.module_id.c_str());
  ImGui::Text("unit:%s   pitch_x:%d", seq.unit.c_str(), seq.pitch_x);
  ImGui::Text("grid:%d x %d (w x h)   幀數:%zu", seq.grid_w, seq.grid_h, seq.dacs.size());
  if (!seq.dacs.empty())
    ImGui::Text("dacs:[%d ... %d]", seq.dacs.front(), seq.dacs.back());
  ImGui::Text("quality 面:%s", seq.has_quality ? "有" : "無");
  ImGui::End();
}

void draw_issues_panel(ViewerState& s) {
  ImGui::Begin("診斷");
  if (!s.loaded) { ImGui::TextDisabled("尚未開啟檔案"); ImGui::End(); return; }
  ImGui::Text("issues:%zu", s.result.issues.size());
  ImGui::Separator();
  for (const auto& is : s.result.issues) {
    const bool err = is.severity == Severity::error;
    ImGui::TextColored(err ? kErrCol : kWarnCol, "[%s] %s", err ? "error" : "warn",
                       is.message.c_str());
  }
  if (s.result.issues.empty() && s.result.ok) ImGui::TextColored(kOkCol, "無 issue");
  ImGui::End();
}

// 泛化版 heatmap(任意 grid_w x grid_h):row=0 於上方、點擊換算、選區高亮框
// (與 dcc_gui 之 DCC/誤差 map 同一視覺慣例)。
void heatmap_plot(ViewerState& s, const char* title, const std::vector<double>& vals,
                  double vmin, double vmax, const char* fmt) {
  const int gw = s.result.seq.grid_w, gh = s.result.seq.grid_h;
  if (ImPlot::BeginPlot(title, ImVec2(-1, 280), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
    ImPlot::SetupAxes(nullptr, nullptr,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock);
    ImPlot::SetupAxesLimits(0, gw, 0, gh, ImGuiCond_Always);
    ImPlot::PlotHeatmap(title, vals.data(), gh, gw, vmin, vmax, fmt, ImPlotPoint(0, 0),
                        ImPlotPoint(gw, gh));

    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const ImPlotPoint p = ImPlot::GetPlotMousePos();
      const int col = static_cast<int>(std::floor(p.x));
      const int row = (gh - 1) - static_cast<int>(std::floor(p.y));
      if (row >= 0 && row < gh && col >= 0 && col < gw) { s.sel_r = row; s.sel_c = col; }
    }
    ImPlot::PushPlotClipRect();
    const ImVec2 a = ImPlot::PlotToPixels(ImPlotPoint(s.sel_c, gh - 1 - s.sel_r));
    const ImVec2 b = ImPlot::PlotToPixels(ImPlotPoint(s.sel_c + 1, gh - s.sel_r));
    ImPlot::GetPlotDrawList()->AddRect(a, b, IM_COL32(255, 70, 0, 255), 0.0f, 0, 2.5f);
    ImPlot::PopPlotClipRect();
    ImPlot::EndPlot();
  }
}

void draw_grid_overview(ViewerState& s) {
  ImGui::Begin("區域總覽(有效樣本數)");
  if (!s.loaded || !s.result.ok) {
    ImGui::TextDisabled("尚未載入或結構不足以繪圖");
    ImGui::End();
    return;
  }
  const auto& seq = s.result.seq;
  const size_t cells = static_cast<size_t>(seq.grid_w) * static_cast<size_t>(seq.grid_h);
  std::vector<double> valid(cells, 0.0);
  for (const auto& frame : seq.data)
    for (size_t i = 0; i < cells; ++i)
      if (!std::isnan(frame[i])) valid[i] += 1.0;

  ImPlot::PushColormap(ImPlotColormap_Viridis);
  heatmap_plot(s, "有效樣本數", valid, 0.0, static_cast<double>(seq.dacs.size()), "%.0f");
  ImPlot::PopColormap();
  ImGui::Text("點擊格子選擇區域;目前:(r=%d, c=%d)。grid=%dx%d,共 %zu 幀", s.sel_r, s.sel_c,
              seq.grid_w, seq.grid_h, seq.dacs.size());
  ImGui::End();
}

void draw_region_detail(ViewerState& s) {
  ImGui::Begin("區域檢視");
  if (!s.loaded || !s.result.ok) { ImGui::TextDisabled("尚未載入"); ImGui::End(); return; }
  const auto& seq = s.result.seq;

  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("r", &s.sel_r, 0, seq.grid_h - 1);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("c", &s.sel_c, 0, seq.grid_w - 1);

  const size_t ri = static_cast<size_t>(s.sel_r) * static_cast<size_t>(seq.grid_w) +
                    static_cast<size_t>(s.sel_c);
  const size_t n = seq.dacs.size();
  std::vector<double> xs, ys, fx(n), fy(n), qx, qy;
  for (size_t f = 0; f < n; ++f) {
    fx[f] = static_cast<double>(seq.dacs[f]);
    fy[f] = seq.focus[f][ri];
    const double d = seq.data[f][ri];
    if (!std::isnan(d)) { xs.push_back(d); ys.push_back(static_cast<double>(seq.dacs[f])); }
    if (seq.has_quality) {
      const double q = seq.quality[f][ri];
      if (!std::isnan(q)) { qx.push_back(static_cast<double>(seq.dacs[f])); qy.push_back(q); }
    }
  }

  const std::string x_label = "disparity [" + seq.unit + "]";
  if (ImPlot::BeginPlot("disparity vs DAC", ImVec2(-1, 210))) {
    ImPlot::SetupAxes(x_label.c_str(), "DAC", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    ImPlot::PlotScatter("樣本", xs.data(), ys.data(), static_cast<int>(xs.size()));
    ImPlot::EndPlot();
  }
  if (ImPlot::BeginPlot("focus vs DAC", ImVec2(-1, 210))) {
    ImPlot::SetupAxes("DAC", "focus value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    ImPlot::PlotLine("fv", fx.data(), fy.data(), static_cast<int>(n));
    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4, kSampleBlue, IMPLOT_AUTO, kSampleBlue);
    ImPlot::PlotScatter("採樣點", fx.data(), fy.data(), static_cast<int>(n));
    ImPlot::EndPlot();
  }
  if (seq.has_quality && ImPlot::BeginPlot("quality vs DAC", ImVec2(-1, 210))) {
    ImPlot::SetupAxes("DAC", "quality", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Always);
    ImPlot::PlotLine("q", qx.data(), qy.data(), static_cast<int>(qx.size()));
    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4, kSampleBlue, IMPLOT_AUTO, kSampleBlue);
    ImPlot::PlotScatter("採樣點", qx.data(), qy.data(), static_cast<int>(qx.size()));
    ImPlot::EndPlot();
  }
  ImGui::End();
}

}  // namespace

void draw_all(ViewerState& s) {
  draw_file_panel(s);
  draw_issues_panel(s);
  draw_grid_overview(s);
  draw_region_detail(s);
}

}  // namespace dcc::seqview

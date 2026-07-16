// DCC 校正桌面工作台(M1b):GLFW + OpenGL3 + Dear ImGui(docking)+ ImPlot。
// --smoke:隱藏視窗渲染 5 幀後結束(建置/啟動煙霧測試)。
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder(自動排版)
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <implot.h>

#include "dcc_gui_common/theme.hpp"
#include "dcc_io/config.hpp"
#include "gui_state.hpp"
#include "panels.hpp"

namespace {

void glfw_error_cb(int code, const char* desc) {
  std::fprintf(stderr, "GLFW 錯誤 %d:%s\n", code, desc);
}

// 主題/字型初始化見 dcc_gui_common/theme.hpp(與 dcc_seqview 共用,避免視覺漂移)。
void load_cjk_font(dcc::gui::GuiState& state) {
  for (const auto& m : dcc::guicommon::load_cjk_font()) {
    const bool is_warn = m.rfind("[warn]", 0) == 0;
    state.log_add(is_warn ? dcc::gui::LogLevel::warn : dcc::gui::LogLevel::info,
                 is_warn ? m.substr(7) : m);
  }
}

// 自動排版(操作動線):左 = 輸入(Sim/Config)→ 中 = 圖表(map/區域)→
// 右 = 判定(總覽)→ 下 = log。
void apply_auto_layout(ImGuiID dockspace_id) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, vp->WorkSize);

  ImGuiID center = dockspace_id;
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.20f, nullptr, &center);
  ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.26f, nullptr, &center);
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
  const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.42f, nullptr, &left);
  const ImGuiID center_right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.50f, nullptr, &center);

  ImGui::DockBuilderDockWindow("Sim 工作台", left);
  ImGui::DockBuilderDockWindow("Config", left_bottom);
  ImGui::DockBuilderDockWindow("DCC / 誤差 map", center);
  ImGui::DockBuilderDockWindow("靈敏度掃描", center);  // 與 map 同節點成分頁
  ImGui::DockBuilderDockWindow("RAW 檢視", center);
  ImGui::DockBuilderDockWindow("區域檢視", center_right);
  ImGui::DockBuilderDockWindow("結果總覽", right);
  ImGui::DockBuilderDockWindow("報告", right);  // 與總覽同節點成分頁
  ImGui::DockBuilderDockWindow("Log 主控台", bottom);
  ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

int main(int argc, char** argv) {
  bool smoke = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;

  glfwSetErrorCallback(glfw_error_cb);
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

  GLFWwindow* window = glfwCreateWindow(1600, 1000, "DCC 校正工作台", nullptr, nullptr);
  if (!window) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  // 首次啟動(無佈局檔)→ 進場自動排版。
  const bool first_run = !std::filesystem::exists("imgui.ini");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  dcc::guicommon::apply_plot_style();  // auto-fit 10% 邊距(與 dcc_seqview 一致)
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  bool light_theme = true;  // 預設淡色調
  dcc::guicommon::apply_theme(light_theme);

  dcc::gui::GuiState state;
  state.cfg = dcc::io::load_config(dcc::io::default_config_json());
  state.file_logger = dcc::io::Logger::create("logs");
  if (state.file_logger)
    state.log_add(dcc::gui::LogLevel::info,
                  "檔案 log:" + state.file_logger->base_path() + ".{log,jsonl}");
  load_cjk_font(state);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");

  bool want_layout = first_run;
  int frames = 0;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport();
    if (want_layout) { apply_auto_layout(dockspace_id); want_layout = false; }

    // 首幀 glyph 自檢:UI 會用到的特殊符號若缺字,直接記 log(不再猜「?」)。
    static bool glyph_checked = false;
    if (!glyph_checked && ImGui::GetFont()) {
      glyph_checked = true;
      const std::string missing = dcc::guicommon::glyph_self_check();
      if (missing.empty())
        state.log_add(dcc::gui::LogLevel::info, "glyph 自檢:σ Δ → ● ² ≈ 全部可顯示");
      else
        state.log_add(dcc::gui::LogLevel::warn, "glyph 自檢:字型缺字" + missing);
    }

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("檔案")) {
        if (ImGui::MenuItem("儲存 Session(session.json)")) state.save_session("session.json");
        if (ImGui::MenuItem("載入 Session")) state.load_session("session.json");
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("診斷")) {
        ImGui::TextDisabled("錯誤情境演練(設定 Sim 參數後自動重跑)");
        if (ImGui::MenuItem("E-D03:角落區有效樣本不足")) {
          state.null_frames = 3; state.dirty = true;
        }
        if (ImGui::MenuItem("E-E01:L/R 顛倒(斜率為負)")) {
          state.spec.center_dcc = -12.46; state.spec.corner_dcc = -14.5; state.dirty = true;
        }
        if (ImGui::MenuItem("E-F01:合焦出界(640)")) {
          state.spec.focus_center = 640.0; state.dirty = true;
        }
        if (ImGui::MenuItem("E-F02:誤差超容差(峰值偏移 100)")) {
          state.spec.focus_peak_offset = 100.0; state.dirty = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("還原標準情境")) {
          const auto keep_dacs = state.spec.dacs;
          state.spec = dcc::sim::SynthSpec{};
          state.spec.dacs = keep_dacs;
          state.null_frames = 0;
          state.fine_grid = false;
          state.dirty = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("E-A01/C01/D01/D02/G01 由測試套件覆蓋(UT-01/02/08)");
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("檢視")) {
        if (ImGui::MenuItem("自動排版(依操作動線)")) want_layout = true;
        ImGui::Separator();
        if (ImGui::MenuItem("淡色主題", nullptr, light_theme) && !light_theme) {
          light_theme = true; dcc::guicommon::apply_theme(true);
        }
        if (ImGui::MenuItem("深色主題", nullptr, !light_theme) && light_theme) {
          light_theme = false; dcc::guicommon::apply_theme(false);
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (state.dirty && state.auto_run) state.regenerate_and_run();
    dcc::gui::draw_all(state);

    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    if (light_theme) glClearColor(0.93f, 0.94f, 0.95f, 1.0f);
    else glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);

    if (smoke && ++frames >= 5) break;
  }

  if (smoke) {
    const bool ok = state.has_result && state.result.pass && state.error_code.empty();
    std::printf("SMOKE %s:has_result=%d pass=%d error=%s regions=%zu\n", ok ? "OK" : "FAIL",
                state.has_result ? 1 : 0,
                (state.has_result && state.result.pass) ? 1 : 0,
                state.error_code.empty() ? "-" : state.error_code.c_str(),
                state.has_result ? state.result.regions.size() : 0);
    for (const auto& e : state.log) std::printf("LOG[%d] %s\n", static_cast<int>(e.level), e.msg.c_str());
    if (!ok) return 1;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

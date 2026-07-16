// dcc_seqview:獨立 disp_seq.json 檢視器(SPEC-004 §3a)。
// 與 dcc_core/dcc_io/dcc_app/dcc_sim 零依賴(見 seq_loader.hpp)——供外部團隊
// 自產序列離線瀏覽/初步結構自檢,不需 build 完整校正引擎。介面風格與 dcc_gui
// 共用 dcc_gui_common/theme.hpp,保持視覺一致。
// 用法:dcc_seqview [disp_seq.json] [--smoke]
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
#include "dcc_seqview/viewer_state.hpp"
#include "panels.hpp"

namespace {

void glfw_error_cb(int code, const char* desc) {
  std::fprintf(stderr, "GLFW 錯誤 %d:%s\n", code, desc);
}

void apply_auto_layout(ImGuiID dockspace_id) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, vp->WorkSize);

  ImGuiID center = dockspace_id;
  ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.28f, nullptr, &center);
  const ImGuiID left_bottom =
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.42f, nullptr, &center);

  ImGui::DockBuilderDockWindow("開啟檔案", left);
  ImGui::DockBuilderDockWindow("診斷", left_bottom);
  ImGui::DockBuilderDockWindow("區域總覽(有效樣本數)", center);
  ImGui::DockBuilderDockWindow("區域檢視", right);
  ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

int main(int argc, char** argv) {
  bool smoke = false;
  std::string initial_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
    else initial_path = argv[i];
  }

  glfwSetErrorCallback(glfw_error_cb);
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

  GLFWwindow* window = glfwCreateWindow(1280, 860, "disp_seq.json 檢視器", nullptr, nullptr);
  if (!window) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  // 獨立佈局檔(與 dcc_gui 的 imgui.ini 分開,避免互相覆蓋)。
  const bool first_run = !std::filesystem::exists("seqview_imgui.ini");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  dcc::guicommon::apply_plot_style();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = "seqview_imgui.ini";
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  bool light_theme = true;  // 預設淡色調(與 dcc_gui 一致)
  dcc::guicommon::apply_theme(light_theme);
  // 字型訊息:成功載入為資訊(→ stdout),僅 [warn](找不到字型)進 stderr,
  // 避免正常路徑的成功訊息在終端機看起來像錯誤。
  for (const auto& m : dcc::guicommon::load_cjk_font()) {
    const bool is_warn = m.rfind("[warn]", 0) == 0;
    std::fprintf(is_warn ? stderr : stdout, "%s\n", is_warn ? m.c_str() + 7 : m.c_str());
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");

  dcc::seqview::ViewerState state;
  if (!initial_path.empty()) {
    std::strncpy(state.path, initial_path.c_str(), sizeof(state.path) - 1);
    state.open(initial_path);
  }

  bool want_layout = first_run;
  int frames = 0;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport();
    if (want_layout) { apply_auto_layout(dockspace_id); want_layout = false; }

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("檢視")) {
        if (ImGui::MenuItem("自動排版")) want_layout = true;
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

    dcc::seqview::draw_all(state);

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
    const bool ok = initial_path.empty() || (state.loaded && state.result.ok);
    std::printf("SMOKE %s:loaded=%d ok=%d issues=%zu\n", ok ? "OK" : "FAIL",
               state.loaded ? 1 : 0, state.result.ok ? 1 : 0, state.result.issues.size());
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

// DCC 校正桌面工作台(M1b 骨架):GLFW + OpenGL3 + Dear ImGui(docking)+ ImPlot。
// --smoke:隱藏視窗渲染 5 幀後結束(建置/啟動煙霧測試)。
#include <cstdio>
#include <cstring>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <implot.h>

#include "dcc_io/config.hpp"
#include "gui_state.hpp"
#include "panels.hpp"

namespace {

void glfw_error_cb(int code, const char* desc) {
  std::fprintf(stderr, "GLFW 錯誤 %d:%s\n", code, desc);
}

// 系統字型(繁中):依平台候選清單載入,全部失敗則退回內建字型(僅英數)。
void load_cjk_font(dcc::gui::GuiState& state) {
  const char* candidates[] = {
      "/System/Library/Fonts/PingFang.ttc",                    // macOS
      "/System/Library/Fonts/Hiragino Sans GB.ttc",            // macOS(備援)
      "C:/Windows/Fonts/msjh.ttc",                             // Windows 微軟正黑
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc" // Linux Noto
  };
  ImGuiIO& io = ImGui::GetIO();
  for (const char* path : candidates) {
    FILE* f = std::fopen(path, "rb");
    if (!f) continue;
    std::fclose(f);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (io.Fonts->AddFontFromFileTTF(path, 18.0f, &cfg,
                                     io.Fonts->GetGlyphRangesChineseFull())) {
      state.log_add(dcc::gui::LogLevel::info, std::string("字型:") + path);
      return;
    }
  }
  state.log_add(dcc::gui::LogLevel::warn, "找不到繁中系統字型,退回內建字型(中文將無法顯示)");
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

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  dcc::gui::GuiState state;
  state.cfg = dcc::io::load_config(dcc::io::default_config_json());
  load_cjk_font(state);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");

  int frames = 0;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport();
    if (state.dirty && state.auto_run) state.regenerate_and_run();
    dcc::gui::draw_all(state);

    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
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

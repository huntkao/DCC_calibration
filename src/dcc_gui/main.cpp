// DCC 校正桌面工作台(M1b):GLFW + OpenGL3 + Dear ImGui(docking)+ ImPlot。
// --smoke:隱藏視窗渲染 5 幀後結束(建置/啟動煙霧測試)。
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder(自動排版)
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

  // ChineseFull 未涵蓋希臘字母(σ)、箭頭(→)、數學符號(Δ 屬希臘)、
  // 幾何圖形(●)等 → 以 builder 補齊,否則顯示為 "?"。
  static ImVector<ImWchar> ranges;  // 必須存活至字型圖集建置完成
  if (ranges.empty()) {
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    static const ImWchar extra[] = {
        0x0370, 0x03FF,  // 希臘字母(σ、Δ…)
        0x2190, 0x21FF,  // 箭頭(→…)
        0x2200, 0x22FF,  // 數學運算子(≈、≤…)
        0x2500, 0x257F,  // 框線
        0x25A0, 0x25FF,  // 幾何圖形(●…)
        0,
    };
    b.AddRanges(extra);
    b.BuildRanges(&ranges);
  }

  for (const char* path : candidates) {
    FILE* f = std::fopen(path, "rb");
    if (!f) continue;
    std::fclose(f);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (io.Fonts->AddFontFromFileTTF(path, 15.5f, &cfg, ranges.Data)) {
      state.log_add(dcc::gui::LogLevel::info, std::string("字型:") + path);
      return;
    }
  }
  state.log_add(dcc::gui::LogLevel::warn, "找不到繁中系統字型,退回內建字型(中文將無法顯示)");
}

void apply_theme(bool light) {
  if (light) {
    ImGui::StyleColorsLight();
    ImPlot::StyleColorsLight();
  } else {
    ImGui::StyleColorsDark();
    ImPlot::StyleColorsDark();
  }
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = 4.0f;
  st.FrameRounding = 3.0f;
  st.TabRounding = 3.0f;
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
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  bool light_theme = true;  // 預設淡色調
  apply_theme(light_theme);

  dcc::gui::GuiState state;
  state.cfg = dcc::io::load_config(dcc::io::default_config_json());
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

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("檢視")) {
        if (ImGui::MenuItem("自動排版(依操作動線)")) want_layout = true;
        ImGui::Separator();
        if (ImGui::MenuItem("淡色主題", nullptr, light_theme) && !light_theme) {
          light_theme = true; apply_theme(true);
        }
        if (ImGui::MenuItem("深色主題", nullptr, !light_theme) && light_theme) {
          light_theme = false; apply_theme(false);
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

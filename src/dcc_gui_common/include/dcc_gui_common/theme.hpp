// 共用 ImGui/ImPlot 主題與字型初始化(header-only,僅 imgui/implot API)。
// 供 dcc_gui 與 dcc_seqview 共用,確保兩支獨立程式視覺/字型一致而不必各自複製漂移。
// 零依賴 dcc_core/dcc_io/dcc_app/dcc_sim——不構成業務邏輯耦合,呼叫端自行決定如何記錄
// 回傳訊息(各工具的 log 面板型別不同,故以字串向量回傳而非直接呼叫特定 logger)。
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>
#include <implot.h>

namespace dcc::guicommon {

// 系統字型(繁中):依平台候選清單載入,全部失敗則退回內建字型(僅英數)。
// 回傳訊息列(info/warn 由呼叫端自行分級記錄)。
inline std::vector<std::string> load_cjk_font(float size_px = 15.5f) {
  std::vector<std::string> messages;
  const char* candidates[] = {
      "/System/Library/Fonts/PingFang.ttc",                     // macOS
      "/System/Library/Fonts/Hiragino Sans GB.ttc",             // macOS(備援)
      "C:/Windows/Fonts/msjh.ttc",                              // Windows 微軟正黑
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"  // Linux Noto
  };
  ImGuiIO& io = ImGui::GetIO();

  // ChineseFull 未涵蓋希臘字母(σ)、箭頭(→)、數學符號(≈)、幾何圖形(●)等
  // → 以 builder 補齊,否則顯示為 "?"。
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

  const auto file_exists = [](const char* p) {
    FILE* f = std::fopen(p, "rb");
    if (f) std::fclose(f);
    return f != nullptr;
  };

  bool loaded = false;
  for (const char* path : candidates) {
    if (!file_exists(path)) continue;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (io.Fonts->AddFontFromFileTTF(path, size_px, &cfg, ranges.Data)) {
      messages.push_back(std::string("字型:") + path);
      loaded = true;
      break;
    }
  }
  if (!loaded) {
    messages.push_back("[warn] 找不到繁中系統字型,退回內建字型(中文將無法顯示)");
    return messages;
  }

  // 部分 CJK 字型缺符號 glyph(如 Hiragino Sans GB 缺 ²)→ 以 MergeMode
  // 從符號字型補齊,不影響 CJK 主字型。
  ImFontConfig mc;
  mc.MergeMode = true;
  static const ImWchar latin_sup[] = {0x00A0, 0x00FF, 0};  // ²、±、° 等
  static const ImWchar sym[] = {0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0};
  const char* latin_fonts[] = {"/System/Library/Fonts/Helvetica.ttc", "C:/Windows/Fonts/arial.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  const char* sym_fonts[] = {"/System/Library/Fonts/Apple Symbols.ttf",
                             "C:/Windows/Fonts/seguisym.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  for (const char* p : latin_fonts)
    if (file_exists(p)) { io.Fonts->AddFontFromFileTTF(p, size_px, &mc, latin_sup); break; }
  for (const char* p : sym_fonts)
    if (file_exists(p)) { io.Fonts->AddFontFromFileTTF(p, size_px, &mc, sym); break; }
  return messages;
}

inline void apply_theme(bool light) {
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

// 所有 plot 之 auto-fit 統一留 10% 邊距,避免資料點/曲線貼邊被遮
// (與 dcc_gui 一致;呼叫端於 ImPlot::CreateContext() 後呼叫一次)。
inline void apply_plot_style() { ImPlot::GetStyle().FitPadding = ImVec2(0.10f, 0.10f); }

// 首幀 glyph 自檢:UI 常用符號若缺字,直接回傳缺字清單(不再猜「?」)。
// 呼叫端應僅在第一個有字型的畫面呼叫一次。
inline std::string glyph_self_check() {
  if (!ImGui::GetFont()) return {};
  const ImWchar probes[] = {0x00B2 /*²*/, 0x03C3 /*σ*/, 0x0394 /*Δ*/,
                            0x2192 /*→*/, 0x25CF /*●*/, 0x2248 /*≈*/};
  std::string missing;
  for (ImWchar w : probes)
    if (!ImGui::GetFont()->FindGlyphNoFallback(w)) {
      char hex[16];
      std::snprintf(hex, sizeof(hex), " U+%04X", static_cast<unsigned>(w));
      missing += hex;
    }
  return missing;  // 空字串 = 全部可顯示
}

}  // namespace dcc::guicommon

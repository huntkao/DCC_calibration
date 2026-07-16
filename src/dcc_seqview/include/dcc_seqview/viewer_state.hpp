// dcc_seqview 之 UI 狀態(單一真值來源)。刻意零依賴 dcc_core/dcc_io/dcc_app/dcc_sim
// ——本工具只讀 disp_seq.json 原始粒度並繪圖,不跑校正引擎(見 seq_loader.hpp)。
#pragma once

#include <string>

#include "dcc_seqview/seq_loader.hpp"

namespace dcc::seqview {

struct ViewerState {
  char path[512] = "";  // 待開啟之 disp_seq.json 路徑
  bool loaded = false;  // 是否已嘗試載入過(即便失敗/有 issue)
  LoadResult result;    // 最近一次載入結果(seq + issues)

  int sel_r = 0, sel_c = 0;  // 區域檢視選擇

  // 載入指定路徑,更新 result/loaded,並將選取區域夾回新 grid 範圍內。
  void open(const std::string& p);
};

}  // namespace dcc::seqview

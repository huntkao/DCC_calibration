// dcc_seqview 各 docking 面板之繪製(僅讀寫 ViewerState;不含解析邏輯,見 seq_loader.hpp)。
#pragma once

#include "dcc_seqview/viewer_state.hpp"

namespace dcc::seqview {
void draw_all(ViewerState& s);
}

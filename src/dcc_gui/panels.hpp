// 各 docking 面板之繪製(僅讀寫 GuiState;不含演算法)。
#pragma once

#include "gui_state.hpp"

namespace dcc::gui {
void draw_all(GuiState& s);
}

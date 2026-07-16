// 原生「開啟檔案」對話框包裝(tinyfiledialogs,zlib license,vendored via FetchContent)。
// 阻塞呼叫——期間會暫停呼叫端的事件迴圈,是原生 modal 對話框之預期行為。
// 零依賴 dcc_core/dcc_io/dcc_app/dcc_sim,供 dcc_gui 與 dcc_seqview 共用。
#pragma once

#include <string>
#include <vector>

namespace dcc::guicommon {

// title/default_path 可為空字串;filter_patterns 例如 {"*.json"}(空 = 不限副檔名)。
// 使用者取消,或平台無可用對話框實作 → 回傳空字串。
std::string open_file_dialog(const std::string& title, const std::string& default_path,
                             const std::vector<std::string>& filter_patterns = {},
                             const std::string& filter_description = "");

}  // namespace dcc::guicommon

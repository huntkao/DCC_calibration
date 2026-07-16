#include "dcc_gui_common/file_dialog.hpp"

#include <tinyfiledialogs.h>

namespace dcc::guicommon {

std::string open_file_dialog(const std::string& title, const std::string& default_path,
                             const std::vector<std::string>& filter_patterns,
                             const std::string& filter_description) {
  std::vector<const char*> patterns;
  patterns.reserve(filter_patterns.size());
  for (const auto& p : filter_patterns) patterns.push_back(p.c_str());

  const char* result = tinyfd_openFileDialog(
      title.c_str(), default_path.c_str(), static_cast<int>(patterns.size()),
      patterns.empty() ? nullptr : patterns.data(),
      filter_description.empty() ? nullptr : filter_description.c_str(),
      /*aAllowMultipleSelects=*/0);
  return result ? std::string(result) : std::string();
}

}  // namespace dcc::guicommon

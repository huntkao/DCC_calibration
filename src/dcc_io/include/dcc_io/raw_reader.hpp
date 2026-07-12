// RAW 唯讀載入(FR-18):僅供 UI 底圖顯示,禁止進入 core 計算路徑;
// 本工具在任何情況下不得寫入/修改 RAW。
// 格式:uint16 little-endian,row-major,無檔頭;尺寸由 config 提供。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dcc::io {

struct RawImage {
  int width = 0, height = 0;
  std::vector<std::uint16_t> pixels;  // row-major,值域依 bit_depth(10-bit → 0..1023)
};

// 缺檔或大小與 W×H×2 不符 → nullopt(降級顯示,不中止管線)。
std::optional<RawImage> load_raw(const std::string& path, int width, int height);

}  // namespace dcc::io

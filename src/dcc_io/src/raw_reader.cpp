#include "dcc_io/raw_reader.hpp"

#include <fstream>

namespace dcc::io {

std::optional<RawImage> load_raw(const std::string& path, int width, int height) {
  if (width <= 0 || height <= 0) return std::nullopt;
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;

  const auto file_size = static_cast<std::uint64_t>(f.tellg());
  const std::uint64_t expect =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 2u;
  if (file_size != expect) return std::nullopt;  // 尺寸閉環驗算不符 → 降級

  RawImage img;
  img.width = width;
  img.height = height;
  img.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(img.pixels.data()), static_cast<std::streamsize>(expect));
  if (!f) return std::nullopt;
  return img;
}

}  // namespace dcc::io

// RAW 合成生成器(dcc_sim)與唯讀讀取器(dcc_io,FR-18)測試。
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "dcc_io/raw_reader.hpp"
#include "dcc_sim/synth.hpp"

namespace fs = std::filesystem;

namespace {
dcc::sim::RawSpec small_spec() {
  dcc::sim::RawSpec rs;
  rs.width = 64;   // 2×2 pattern 週期,測試夠用
  rs.height = 64;
  return rs;
}
}  // namespace

TEST_CASE("generate_raw:尺寸閉環、值域 0..1023、確定性", "[raw][sim]") {
  const auto rs = small_spec();
  const auto px = dcc::sim::generate_raw(rs);
  REQUIRE(px.size() == 64u * 64u);
  for (auto v : px) REQUIRE(v <= 1023);
  REQUIRE(px == dcc::sim::generate_raw(rs));  // 同 seed 同輸出
}

TEST_CASE("generate_raw:PD 像素受 metal-shield 遮光", "[raw][sim]") {
  const auto rs = small_spec();
  const auto px = dcc::sim::generate_raw(rs);
  // PD L 位於 (4,3);(4,5) 為同一 x(同紋理相位)之一般像素。
  const auto pd = px[3u * 64u + 4u];
  const auto normal = px[5u * 64u + 4u];
  REQUIRE(static_cast<double>(pd) < 0.7 * static_cast<double>(normal));
  // 遮光後仍高於 black level(非全黑)。
  REQUIRE(pd > 64);
}

TEST_CASE("load_raw:round-trip 一致;尺寸不符/缺檔 → nullopt(降級)", "[raw][io]") {
  const auto rs = small_spec();
  const auto px = dcc::sim::generate_raw(rs);

  const fs::path dir = fs::temp_directory_path() / "dcc_raw_test";
  fs::create_directories(dir);
  const fs::path p = dir / "frame_00_dac0180.raw";
  {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(px.data()),
            static_cast<std::streamsize>(px.size() * 2));
  }

  const auto img = dcc::io::load_raw(p.string(), 64, 64);
  REQUIRE(img.has_value());
  REQUIRE(img->width == 64);
  REQUIRE(img->pixels == px);

  REQUIRE_FALSE(dcc::io::load_raw(p.string(), 128, 128).has_value());  // 尺寸閉環不符
  REQUIRE_FALSE(dcc::io::load_raw((dir / "missing.raw").string(), 64, 64).has_value());
}

#pragma once

#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/texture_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

// Plan 0045 P9 (Spec 0045, ruling O2): synthetic BC7 mip chains built at
// test time and cooked through the real cookTextureBc7() into .atex v3 --
// nothing binary is committed. Every block is BC7 mode 6 (one subset,
// 7-bit RGBA endpoints plus one p-bit each, 4-bit indices), the encoding
// the glTF importer's white fallback already uses
// (material_import.cpp's whiteFallbackDds()). An endpoint channel is
// (7-bit value << 1) | p-bit with one p-bit per endpoint, so every colour
// here has all four channels odd (p-bit 1): index 0 decodes to endpoint 0
// and index 15 to endpoint 1 exactly.
//
// Two chains:
// - level-colour: every level one distinct solid colour, so a sampled
//   texel names the level it came from (Plan 0045 P10);
// - checker: level 0 a 1-texel checker of (1,1,1) and (253,253,253),
//   every level below it their exact box average (127,127,127) -- what a
//   correct authoring tool stores for a 1-texel checker (P11).

namespace atlantis::image_regression {

using Bc7Block = std::array<std::uint8_t, 16>;
using Rgba8 = std::array<std::uint8_t, 4>;

// Mode-6 block from two endpoints (all four channels of each odd) and
// sixteen 4-bit indices, row-major; the anchor (index 0) must be < 8.
[[nodiscard]] inline Bc7Block bc7Mode6Block(const Rgba8& endpoint0, const Rgba8& endpoint1,
                                           const std::array<std::uint8_t, 16>& indices) {
  Bc7Block block{};
  std::size_t bit = 0;
  const auto put = [&](std::uint32_t value, std::size_t bits) {
    for (std::size_t i = 0; i < bits; ++i, ++bit) {
      if ((value >> i) & 1U) block[bit / 8] = static_cast<std::uint8_t>(block[bit / 8] | (1U << (bit % 8)));
    }
  };
  put(1U << 6, 7);  // mode 6: six 0 bits, then a 1
  for (std::size_t channel = 0; channel < 4; ++channel) {
    put(endpoint0[channel] >> 1, 7);
    put(endpoint1[channel] >> 1, 7);
  }
  put(endpoint0[0] & 1U, 1);  // p-bits
  put(endpoint1[0] & 1U, 1);
  put(indices[0], 3);  // anchor: implied high bit 0
  for (std::size_t i = 1; i < 16; ++i) put(indices[i], 4);
  return block;
}

[[nodiscard]] inline Bc7Block bc7SolidBlock(const Rgba8& colour) { return bc7Mode6Block(colour, colour, {}); }

// Fourteen distinct colours, every channel odd -- enough for an 8192^2
// chain. Level k of a level-colour chain is kMipTestLevelColours[k].
inline constexpr std::array<Rgba8, 14> kMipTestLevelColours = {{
    {255, 1, 1, 255},     {1, 255, 1, 255},     {1, 1, 255, 255},   {255, 255, 1, 255}, {255, 1, 255, 255},
    {1, 255, 255, 255},   {255, 127, 1, 255},   {127, 1, 255, 255}, {1, 127, 127, 255}, {191, 191, 191, 255},
    {63, 63, 63, 255},    {255, 255, 255, 255}, {127, 255, 63, 255}, {63, 127, 255, 255},
}};
inline constexpr Rgba8 kCheckerDark = {1, 1, 1, 255};
inline constexpr Rgba8 kCheckerLight = {253, 253, 253, 255};
inline constexpr Rgba8 kCheckerAverage = {127, 127, 127, 255};  // (1 + 253) / 2 exactly

// mip_chain_demo's checker texture size (Plan 0045 P11): LOD ~2.32 on the
// textured-quad fixture's 204.8 px-wide quads.
inline constexpr std::uint32_t kMipChainDemoTextureSize = 1024;

// A mip chain's bytes: every block of level k is blockForLevel(k, blockX,
// blockY), laid out as asset_system::textureMipLevels() says.
template <typename BlockForLevel>
[[nodiscard]] std::vector<std::uint8_t> buildBc7Chain(std::uint32_t size, std::uint32_t mipCount,
                                                      BlockForLevel blockForLevel) {
  using atlantis::asset_system::TextureDataLayout;
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(
      atlantis::asset_system::textureMipChainByteCount(size, size, TextureDataLayout::Bc7, mipCount)));
  const auto levels = atlantis::asset_system::textureMipLevels(size, size, TextureDataLayout::Bc7, mipCount);
  for (std::uint32_t k = 0; k < mipCount; ++k) {
    const std::uint32_t blocksWide = (levels[k].width + 3) / 4;
    const std::uint32_t blocksHigh = (levels[k].height + 3) / 4;
    std::size_t offset = static_cast<std::size_t>(levels[k].offsetBytes);
    for (std::uint32_t by = 0; by < blocksHigh; ++by) {
      for (std::uint32_t bx = 0; bx < blocksWide; ++bx) {
        const Bc7Block block = blockForLevel(k, bx, by);
        for (std::size_t i = 0; i < 16; ++i) bytes[offset + i] = block[i];
        offset += 16;
      }
    }
  }
  return bytes;
}

// Full chain, level k a solid kMipTestLevelColours[k].
[[nodiscard]] inline std::vector<std::uint8_t> levelColourChain(std::uint32_t size) {
  return buildBc7Chain(size, atlantis::asset_system::fullMipChainLength(size, size),
                       [](std::uint32_t k, std::uint32_t, std::uint32_t) {
                         return bc7SolidBlock(kMipTestLevelColours[k]);
                       });
}

// One level, solid colour -- a level-colour chain's level k on its own.
[[nodiscard]] inline std::vector<std::uint8_t> solidSingleLevel(std::uint32_t size, const Rgba8& colour) {
  return buildBc7Chain(size, 1, [&colour](std::uint32_t, std::uint32_t, std::uint32_t) {
    return bc7SolidBlock(colour);
  });
}

// Full chain: level 0 the 1-texel checker (texel (x, y) dark when x + y
// is even; every block starts at an even texel, so its anchor is dark),
// every other level the checker's box average.
[[nodiscard]] inline std::vector<std::uint8_t> checkerChain(std::uint32_t size) {
  std::array<std::uint8_t, 16> checkerIndices{};
  for (std::size_t i = 0; i < 16; ++i) checkerIndices[i] = ((i % 4) + (i / 4)) % 2 == 0 ? 0 : 15;
  const Bc7Block checker = bc7Mode6Block(kCheckerDark, kCheckerLight, checkerIndices);
  const Bc7Block average = bc7SolidBlock(kCheckerAverage);
  return buildBc7Chain(size, atlantis::asset_system::fullMipChainLength(size, size),
                       [&](std::uint32_t k, std::uint32_t, std::uint32_t) { return k == 0 ? checker : average; });
}

struct CookedMipTestTexture {
  std::filesystem::path artifactPath;
  std::filesystem::path metadataPath;
};

// A per-process scratch directory for cooked synthetic textures (ctest
// runs each TEST_CASE in its own process).
[[nodiscard]] inline std::filesystem::path mipTestScratchDirectory() {
  static const std::filesystem::path directory = [] {
    const auto path = std::filesystem::temp_directory_path() / "atlantis_mip_test_textures" /
                      std::to_string(std::random_device{}());
    std::filesystem::create_directories(path);
    return path;
  }();
  return directory;
}

// Cooks a size x size BC7 chain through cookTextureBc7() as
// <scratch>/<name>.atex (+ .meta.txt); nullopt if the cook fails.
[[nodiscard]] inline std::optional<CookedMipTestTexture> cookMipTestTexture(const std::string& name,
                                                                            std::uint32_t size,
                                                                            const std::vector<std::uint8_t>& chain) {
  using atlantis::asset_system::TextureDataLayout;
  std::uint32_t mipCount = 0;
  for (std::uint32_t count = 1; count <= atlantis::asset_system::fullMipChainLength(size, size); ++count) {
    if (atlantis::asset_system::textureMipChainByteCount(size, size, TextureDataLayout::Bc7, count) ==
        chain.size()) {
      mipCount = count;
    }
  }
  if (mipCount == 0) return std::nullopt;
  const std::filesystem::path directory = mipTestScratchDirectory();
  CookedMipTestTexture cooked{directory / (name + ".atex"), directory / (name + ".atex.meta.txt")};
  const auto result = atlantis::asset_system::cookTextureBc7(
      chain.data(), chain.size(), size, size, mipCount, atlantis::asset_system::TextureColorSpace::Unorm,
      "mip_test/" + name + ".dds", cooked.artifactPath, cooked.metadataPath);
  if (result.isErr()) return std::nullopt;
  return cooked;
}

}  // namespace atlantis::image_regression

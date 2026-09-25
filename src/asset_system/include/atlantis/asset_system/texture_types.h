#pragma once

#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0016 Section D8: Asset System's own, independent color-space
// enum -- deliberately never atlantis::rhi::SampledTextureFormat, and
// this header never includes any RHI header at all (the established
// Asset-System-must-not-depend-on-RHI module boundary). A composition
// root outside Asset System (the fixture, Milestone 9) is the only
// place that translates TextureColorSpace -> SampledTextureFormat.
enum class TextureColorSpace {
  Unorm,
  Srgb,
};

// Spec 0038/ADR-0085: how pixelBytes is packed -- tightly-packed RGBA8
// (Rgba8) or verbatim BC7 blocks, 16 bytes per 4x4-texel block, base-mip
// dimensions always a multiple of 4 (Bc7). Like TextureColorSpace above,
// deliberately never an RHI type; the composition root translates the
// pair into SampledTextureFormat.
enum class TextureDataLayout {
  Rgba8,
  Bc7,
};

// CPU-side result of loadTextureAsset() -- pixelBytes' packing depends
// on layout (Spec 0038): tightly-packed RGBA8, row-major, width * 4
// bytes per row, no padding for Rgba8; verbatim BC7 block bytes,
// ceil(width/4) * ceil(height/4) * 16 bytes per level, for Bc7 (matching
// the artifact's own on-disk contract, texture_artifact.h). Spec 0045 /
// ADR-0093 Decision 2: pixelBytes is the whole mip chain, mipCount levels,
// level 0 first; textureMipLevels() (texture_artifact.h) gives each
// level's extent, offset and size -- never re-derived elsewhere. No RHI type is
// named, included, or constructed anywhere in this file. A composition
// root outside Asset System is responsible for passing this into
// atlantis::rhi::Device::createSampledTexture()/copyBufferToTexture().
struct TextureAssetData {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  TextureDataLayout layout = TextureDataLayout::Rgba8;
  std::uint32_t mipCount = 1;
  std::vector<std::uint8_t> pixelBytes;
};

}  // namespace atlantis::asset_system

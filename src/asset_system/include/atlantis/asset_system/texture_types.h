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

// CPU-side result of loadTextureAsset() -- pixelBytes is always tightly
// packed RGBA8, row-major, width * 4 bytes per row, no padding (matching
// the artifact's own on-disk contract, texture_artifact.h). No RHI type
// is named, included, or constructed anywhere in this file. A
// composition root outside Asset System is responsible for passing this
// into atlantis::rhi::Device::createSampledTexture()/copyBufferToTexture().
struct TextureAssetData {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  std::vector<std::uint8_t> pixelBytes;
};

}  // namespace atlantis::asset_system

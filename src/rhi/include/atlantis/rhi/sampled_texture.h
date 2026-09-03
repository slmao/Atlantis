#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A general sampled color GPU image, independent of the depth-only
// Texture (ADR-0023). Spec 0025/P2 widens the original 2D/single-mip
// contract to either a 2D image or six-layer cubemap with an explicit
// mip count. Move-only, single-owner, held behind
// std::unique_ptr<SampledTexture>. Owned by whoever creates it via
// Device::createSampledTexture() -- Device does not retain a reference
// (ADR-0003), and Material only ever borrows one (ADR-0056). Not
// internally thread-safe; caller-thread-only (ADR-0004).
class SampledTexture {
 public:
  virtual ~SampledTexture() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual SampledTextureFormat format() const = 0;
  [[nodiscard]] virtual SampledTextureDimension dimension() const = 0;
  [[nodiscard]] virtual std::uint32_t mipLevelCount() const = 0;
};

}  // namespace atlantis::rhi

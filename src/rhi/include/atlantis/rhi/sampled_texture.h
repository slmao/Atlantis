#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A general, sampled 2D color GPU image (Spec 0016) -- independent of,
// and unrelated to, the existing depth-only Texture (ADR-0023): that
// type's own "no sampled/shader-read usage" scope is completely
// unmodified by this one. Exactly one mip level this round -- no
// mip-count accessor exists because there is nothing to report beyond
// the fixed value 1. Move-only, single-owner, held behind
// std::unique_ptr<SampledTexture>. Owned by whoever creates it via
// Device::createSampledTexture() -- Device does not retain a reference
// (ADR-0003), and Material only ever borrows one (ADR-0056). Not
// internally thread-safe; caller-thread-only (ADR-0004).
class SampledTexture {
 public:
  virtual ~SampledTexture() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual SampledTextureFormat format() const = 0;
};

}  // namespace atlantis::rhi

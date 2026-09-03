#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// An independent, immutable GPU sampler (Spec 0016, ADR-0055) -- its
// own filter/address configuration is fixed for its entire lifetime;
// no setter of any kind exists anywhere on this interface, a
// type-level guarantee, not a documented convention. Reusable across
// multiple SampledTextures -- nothing ties one Sampler to any single
// texture. Move-only, single-owner, held behind
// std::unique_ptr<Sampler>. Owned by whoever creates it via
// Device::createSampler() (ADR-0003); Material only ever borrows one
// (ADR-0056). Not internally thread-safe; caller-thread-only
// (ADR-0004).
class Sampler {
 public:
  virtual ~Sampler() = default;

  [[nodiscard]] virtual Filter filter() const = 0;
  [[nodiscard]] virtual AddressMode addressMode() const = 0;
  [[nodiscard]] virtual MipFilter mipFilter() const = 0;
  [[nodiscard]] virtual float minLod() const = 0;
  [[nodiscard]] virtual float maxLod() const = 0;
};

}  // namespace atlantis::rhi

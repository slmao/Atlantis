#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A GPU image used, this round, exclusively as a depth attachment
// (ADR-0023) -- no sampled/shader-read usage, no mipmaps. Move-only,
// single-owner, held behind std::unique_ptr<Texture>. Owned and
// resize-recreated by the caller, never by Renderer or Presentation
// (ADR-0022). Not internally thread-safe; caller-thread-only (ADR-0004).
class Texture {
 public:
  virtual ~Texture() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual DepthFormat format() const = 0;
};

}  // namespace atlantis::rhi

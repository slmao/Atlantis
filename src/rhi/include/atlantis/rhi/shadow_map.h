#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// Plan 0027 Milestone 1 (ADR-0072 D-1): a single directional light's own
// depth-only shadow map -- written as a depth attachment by the
// shadow-casting pass, later sampled by the main draw pass while shading
// PbrDirectLit/pbr_ibl surfaces. A new, single-purpose type, distinct
// from RenderTarget/Texture/SampledTexture -- Texture (texture.h) is, by
// its own header comment, "used, this round, exclusively as a depth
// attachment -- no sampled/shader-read usage," the exact gap this type
// closes, mirroring HdrColorTarget's own identical "new type instead of
// widening an existing narrow one" precedent (ADR-0068 D-1). Exposes
// exactly two narrow capabilities via CommandList's own new overloads
// (command_list.h): bind as a depth attachment for the shadow-casting
// pass, and bind as a combined-image-sampler input for the main draw
// pass. Vended by Device::createShadowMap() -- Device does not retain a
// reference (ADR-0003). Fixed resolution, never resized on window/extent
// change (a real, disclosed difference from HdrColorTarget/depth
// Texture, both of which resize with the window). Move-only,
// single-owner, held behind std::unique_ptr<ShadowMap>. Not internally
// thread-safe; caller-thread-only (ADR-0004).
class ShadowMap {
 public:
  virtual ~ShadowMap() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual DepthFormat format() const = 0;
};

}  // namespace atlantis::rhi

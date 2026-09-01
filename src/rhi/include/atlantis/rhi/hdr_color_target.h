#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// Plan 0024 Milestone 1 (ADR-0068 D-1): the scene-referred linear HDR
// color intermediate the geometry pass writes into, later sampled by
// the output-transform pass. A new, single-purpose type, distinct from
// RenderTarget/Texture/SampledTexture -- inheriting neither
// RenderTarget (Guard 2, render_graph::execute(), would then reject
// this type's own required read usage) nor SampledTexture
// (SampledTexture::format() returns SampledTextureFormat, which cannot
// represent HdrFormat) -- see ADR-0068 D-1's own Alternatives
// Considered for the full rationale. Exposes exactly two narrow
// capabilities via CommandList's own new overloads (command_list.h):
// bind as a color-attachment-output target for the geometry pass, and
// bind as a combined-image-sampler input for the output-transform
// pass. Vended by Device::createHdrColorTarget() -- Device does not
// retain a reference (ADR-0003). Move-only, single-owner, held behind
// std::unique_ptr<HdrColorTarget>. Not internally thread-safe;
// caller-thread-only (ADR-0004).
class HdrColorTarget {
 public:
  virtual ~HdrColorTarget() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual HdrFormat format() const = 0;
};

}  // namespace atlantis::rhi

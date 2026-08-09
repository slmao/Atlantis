#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// One presentable color attachment -- the acquired swapchain image and
// whatever Vulkan Backend needs to record/present it (ADR-0019).
// Non-owning: Presentation owns every swapchain-backed resource behind
// it (ADR-0003). Frame-scoped: valid from the acquireNextTarget() call
// that vended it until the matching present() call consumes it -- using
// it outside that window is a lifetime precondition violation, not a
// guaranteed-detectable error. Write-only this round: no method here (or
// on CommandList) ever reads its prior contents. Held exclusively behind
// std::unique_ptr<RenderTarget> (ADR-0014's mechanism); not copyable.
// Not internally thread-safe; caller-thread-only (ADR-0004).
class RenderTarget {
 public:
  virtual ~RenderTarget() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual Format format() const = 0;
};

}  // namespace atlantis::rhi

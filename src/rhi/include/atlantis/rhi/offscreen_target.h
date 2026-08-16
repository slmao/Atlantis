#pragma once

#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// Headless counterpart to Presentation (Spec 0010/ADR-0038): a second
// vendor of the existing RenderTarget type, with no window/surface/
// swapchain involvement of any kind. Constructed via Vulkan Backend's
// factory API (Device::createOffscreenTarget()); owns its color image,
// memory, and view for its entire lifetime. Must be destroyed before the
// Device it was constructed from (caller-enforced, same tier as every
// other Vulkan Backend type). Not internally thread-safe; caller-thread-
// only (ADR-0004).
//
// Lifetime contract (ADR-0038's Lifetime contract section is the full,
// authoritative statement -- summarized here): a vended RenderTarget
// borrow ends via ordinary RAII (destroying/resetting the returned
// std::unique_ptr<RenderTarget>) -- there is no public release()/
// consume() method. The borrow's minimum required lifetime extends only
// through the Device::submit() call that references it, not through GPU
// completion. Independently, this OffscreenTarget's own backing
// resources must remain alive until GPU work referencing them has
// finished executing -- established by the caller via Device::waitIdle()
// before destroying this object -- deliberately not guaranteed-
// detectable (the same, undetectable tier as Presentation's identical
// existing precondition). Only the double-acquire and destroy-while-
// borrow-outstanding misuse cases are guaranteed-detectable programmer
// errors, via this codebase's existing ATLANTIS_CHECK mechanism.
class OffscreenTarget {
 public:
  virtual ~OffscreenTarget() = default;

  // Two-outcome (ADR-0038): Err is reserved for a genuine unrecoverable,
  // environmental failure, mirroring PresentationError's own non-
  // precondition variants. Calling this while a previously-vended borrow
  // is still outstanding is a guaranteed-detectable programmer error
  // (ATLANTIS_CHECK), not part of this Result::Err channel. Unlike
  // Presentation::acquireNextTarget(), there is no zero-extent case --
  // an OffscreenTarget's extent is fixed at construction time.
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<RenderTarget>, OffscreenAcquireError> acquireTarget() = 0;
};

}  // namespace atlantis::rhi

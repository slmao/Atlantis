#pragma once

#include <memory>
#include <variant>

#include <atlantis/result.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/submission_signal.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// The swapchain-backed drawable-surface abstraction (ADR-0002), scoped to
// its non-frame lifecycle only (ADR-0016). Constructed via Vulkan Backend's
// factory API (ADR-0014) from a Device and a NativeWindowHandle --
// construction creates the VkSurfaceKHR only, never a swapchain. Owns its
// swapchain once one exists. Must be destroyed before the Device it was
// constructed from (caller-enforced; not tracked by this type -- see
// ADR-0003's explicit-ownership, no-hidden-refcounting model). Not
// internally thread-safe; every method here is caller-thread-only, the same
// single Phase 1 frame thread that owns the Windows Platform message pump
// (ADR-0004). Declares no acquire, present, or synchronization primitive of
// any kind -- see ADR-0016.
class Presentation {
 public:
  virtual ~Presentation() = default;

  // Updates the tracked extent and marks recreation needed. Makes no
  // Vulkan call.
  virtual void notifyResized(Extent2D extent) = 0;

  // The only operation that ever creates, recreates, or destroys the
  // backing VkSwapchainKHR. See ADR-0016 for the exact 4-step contract.
  // Issues zero Vulkan swapchain calls whenever the tracked extent is
  // {0, 0} -- true on the first call after construction and every later
  // call alike.
  //
  // atlantis::Result<T, E> is std::variant-backed and cannot hold `void`;
  // std::monostate is the established empty-success-payload idiom this
  // codebase already uses for the identical situation (see
  // atlantis::platform::initialize()) -- not a new decision made here.
  [[nodiscard]] virtual atlantis::Result<std::monostate, PresentationError> recreateIfNeeded() = 0;

  // Reflects the most recently successfully (re)created swapchain. Never
  // hands out an image handle, a RenderTarget, or any per-image resource.
  [[nodiscard]] virtual SwapchainMetadata metadata() const = 0;

  // Tri-state (ADR-0019): Err -- unrecoverable failure (surface/device
  // lost, or a recreateIfNeeded() failure this call performs internally
  // as its first step). Ok(nullptr) -- nothing to draw this frame (zero
  // extent, or an out-of-date swapchain deferred to the next call) --
  // not an error. Ok(non-null) -- a usable RenderTarget. Never retries
  // acquisition within the same call on VK_ERROR_OUT_OF_DATE_KHR.
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<RenderTarget>, PresentationError> acquireNextTarget() = 0;

  // Consumes target (ends its borrow) and waits on renderFinished before
  // calling vkQueuePresentKHR internally. Out-of-date/suboptimal from
  // present itself is routine (marks recreation needed, not Err); any
  // other Vulkan error is a genuine Err.
  [[nodiscard]] virtual atlantis::Result<std::monostate, PresentationError> present(
      std::unique_ptr<RenderTarget> target, std::unique_ptr<SubmissionSignal> renderFinished) = 0;
};

}  // namespace atlantis::rhi

#include <atlantis/runtime/error_classification.h>

#include <atlantis/assert.h>

namespace atlantis::runtime {

RuntimeExitReason classifyPresentationError(atlantis::rhi::PresentationError error) noexcept {
  switch (error) {  // no default -- see Plan 0013 Section D3 (2026-08-21 amendment)
    case atlantis::rhi::PresentationError::SurfaceLost:
    case atlantis::rhi::PresentationError::SwapchainCreationFailed:
    case atlantis::rhi::PresentationError::DeviceLost:
    case atlantis::rhi::PresentationError::Unknown:
      return RuntimeExitReason::UnrecoverableRuntimeError;
  }
  // Reached only if a future PresentationError enumerator is added
  // without a corresponding case above. ATLANTIS_CHECK_MSG's own
  // installed handler is NOT guaranteed to terminate (Core's own
  // replaceable-handler contract) -- a fallback return after it is
  // therefore required, not optional, and must be the same conservative
  // value every other branch already returns. Do NOT turn the switch
  // above into a `default:` case instead: that would additionally
  // suppress the C4062 diagnostic atlantis_runtime_host's /w14062
  // compile option relies on to catch a missing case at build time --
  // see src/runtime/CMakeLists.txt.
  ATLANTIS_CHECK_MSG(false, "classifyPresentationError(): unhandled PresentationError enumerator");
  return RuntimeExitReason::UnrecoverableRuntimeError;
}

RuntimeExitReason classifySubmitError(atlantis::rhi::SubmitError error) noexcept {
  switch (error) {  // no default -- see Plan 0013 Section D3 (2026-08-21 amendment)
    case atlantis::rhi::SubmitError::QueueSubmitFailed:
    case atlantis::rhi::SubmitError::DeviceLost:
      return RuntimeExitReason::UnrecoverableRuntimeError;
  }
  // See classifyPresentationError()'s identical comment above.
  ATLANTIS_CHECK_MSG(false, "classifySubmitError(): unhandled SubmitError enumerator");
  return RuntimeExitReason::UnrecoverableRuntimeError;
}

}  // namespace atlantis::runtime

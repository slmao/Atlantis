#include <atlantis/rhi/types.h>
#include <atlantis/runtime/error_classification.h>
#include <atlantis/runtime/exit_reason.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::PresentationError;
using atlantis::rhi::SubmitError;
using atlantis::runtime::classifyPresentationError;
using atlantis::runtime::classifySubmitError;
using atlantis::runtime::RuntimeExitReason;

// V1 (Plan 0013): every real PresentationError enumerator, confirmed
// exhaustive against src/rhi/include/atlantis/rhi/types.h's actual
// current enum, individually maps to UnrecoverableRuntimeError.
TEST_CASE("classifyPresentationError(SurfaceLost) is UnrecoverableRuntimeError", "[runtime][error_classification]") {
  REQUIRE(classifyPresentationError(PresentationError::SurfaceLost) == RuntimeExitReason::UnrecoverableRuntimeError);
}

TEST_CASE("classifyPresentationError(SwapchainCreationFailed) is UnrecoverableRuntimeError",
          "[runtime][error_classification]") {
  REQUIRE(classifyPresentationError(PresentationError::SwapchainCreationFailed) ==
          RuntimeExitReason::UnrecoverableRuntimeError);
}

TEST_CASE("classifyPresentationError(DeviceLost) is UnrecoverableRuntimeError", "[runtime][error_classification]") {
  REQUIRE(classifyPresentationError(PresentationError::DeviceLost) == RuntimeExitReason::UnrecoverableRuntimeError);
}

TEST_CASE("classifyPresentationError(Unknown) is UnrecoverableRuntimeError", "[runtime][error_classification]") {
  REQUIRE(classifyPresentationError(PresentationError::Unknown) == RuntimeExitReason::UnrecoverableRuntimeError);
}

TEST_CASE("classifySubmitError(QueueSubmitFailed) is UnrecoverableRuntimeError", "[runtime][error_classification]") {
  REQUIRE(classifySubmitError(SubmitError::QueueSubmitFailed) == RuntimeExitReason::UnrecoverableRuntimeError);
}

TEST_CASE("classifySubmitError(DeviceLost) is UnrecoverableRuntimeError", "[runtime][error_classification]") {
  REQUIRE(classifySubmitError(SubmitError::DeviceLost) == RuntimeExitReason::UnrecoverableRuntimeError);
}

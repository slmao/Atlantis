#include "vulkan_presentation.h"

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::Extent2D;
using atlantis::vulkan_backend::PresentationCreateError;
using atlantis::vulkan_backend::detail::checkSurfaceSupported;
using atlantis::vulkan_backend::detail::decideRecreateAction;
using atlantis::vulkan_backend::detail::RecreateAction;

TEST_CASE("decideRecreateAction skips at zero extent regardless of recreationNeeded",
          "[vulkan_backend][presentation_logic]") {
  REQUIRE(decideRecreateAction(Extent2D{0, 0}, true) == RecreateAction::Skip);
  REQUIRE(decideRecreateAction(Extent2D{0, 0}, false) == RecreateAction::Skip);
}

TEST_CASE("decideRecreateAction is a no-op at non-zero extent when recreation is not needed",
          "[vulkan_backend][presentation_logic]") {
  REQUIRE(decideRecreateAction(Extent2D{1920, 1080}, false) == RecreateAction::NoOp);
}

TEST_CASE("decideRecreateAction recreates at non-zero extent when recreation is needed",
          "[vulkan_backend][presentation_logic]") {
  REQUIRE(decideRecreateAction(Extent2D{1920, 1080}, true) == RecreateAction::Recreate);
}

TEST_CASE("checkSurfaceSupported returns no error when the surface is supported",
          "[vulkan_backend][presentation_logic]") {
  REQUIRE_FALSE(checkSurfaceSupported(true).has_value());
}

TEST_CASE("checkSurfaceSupported returns UnsupportedDevice when the surface is not supported",
          "[vulkan_backend][presentation_logic]") {
  const auto result = checkSurfaceSupported(false);
  REQUIRE(result.has_value());
  REQUIRE(*result == PresentationCreateError::UnsupportedDevice);
}

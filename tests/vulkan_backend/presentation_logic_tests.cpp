#include "vulkan_presentation.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::vulkan_backend::PresentationCreateError;
using atlantis::vulkan_backend::detail::checkSurfaceSupported;
using atlantis::vulkan_backend::detail::decideRecreateAction;
using atlantis::vulkan_backend::detail::RecreateAction;
using atlantis::vulkan_backend::detail::selectSurfaceFormat;
using atlantis::vulkan_backend::detail::supportsClearColorImageUsage;
using atlantis::vulkan_backend::detail::supportsRequiredSwapchainUsage;

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

TEST_CASE("selectSurfaceFormat picks the top preference for VK_FORMAT_UNDEFINED with a supported color space",
          "[vulkan_backend][presentation_logic]") {
  const std::vector<VkSurfaceFormatKHR> formats = {{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};

  const auto selection = selectSurfaceFormat(formats);

  REQUIRE(selection.has_value());
  REQUIRE(selection->vkFormat == VK_FORMAT_B8G8R8A8_UNORM);
  REQUIRE(selection->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
  REQUIRE(selection->approvedFormat == Format::Bgra8Unorm);
}

TEST_CASE("selectSurfaceFormat rejects VK_FORMAT_UNDEFINED with an unsupported color space",
          "[vulkan_backend][presentation_logic]") {
  const std::vector<VkSurfaceFormatKHR> formats = {{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT}};

  REQUIRE_FALSE(selectSurfaceFormat(formats).has_value());
}

TEST_CASE("selectSurfaceFormat matches an explicitly listed approved (format, color space) pair",
          "[vulkan_backend][presentation_logic]") {
  const std::vector<VkSurfaceFormatKHR> formats = {{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};

  const auto selection = selectSurfaceFormat(formats);

  REQUIRE(selection.has_value());
  REQUIRE(selection->vkFormat == VK_FORMAT_R8G8B8A8_SRGB);
  REQUIRE(selection->approvedFormat == Format::Rgba8Srgb);
}

TEST_CASE("selectSurfaceFormat does not match an approved format reported with a different color space",
          "[vulkan_backend][presentation_logic]") {
  const std::vector<VkSurfaceFormatKHR> formats = {
      {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT}};

  REQUIRE_FALSE(selectSurfaceFormat(formats).has_value());
}

TEST_CASE("supportsRequiredSwapchainUsage is true when color attachment usage is supported",
          "[vulkan_backend][presentation_logic]") {
  VkSurfaceCapabilitiesKHR capabilities{};
  capabilities.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  REQUIRE(supportsRequiredSwapchainUsage(capabilities));
}

TEST_CASE("supportsRequiredSwapchainUsage is false when color attachment usage is not supported",
          "[vulkan_backend][presentation_logic]") {
  VkSurfaceCapabilitiesKHR capabilities{};
  capabilities.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  REQUIRE_FALSE(supportsRequiredSwapchainUsage(capabilities));
}

TEST_CASE("supportsClearColorImageUsage is true when transfer-dst usage is supported",
          "[vulkan_backend][presentation_logic]") {
  VkSurfaceCapabilitiesKHR capabilities{};
  capabilities.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  REQUIRE(supportsClearColorImageUsage(capabilities));
}

TEST_CASE("supportsClearColorImageUsage is false when transfer-dst usage is not supported",
          "[vulkan_backend][presentation_logic]") {
  VkSurfaceCapabilitiesKHR capabilities{};
  capabilities.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  REQUIRE_FALSE(supportsClearColorImageUsage(capabilities));
}

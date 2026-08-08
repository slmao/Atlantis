#include "validation.h"

#include <string_view>

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::effectiveValidationLayersEnabled;
using atlantis::vulkan_backend::detail::isFatalValidationSeverity;
using atlantis::vulkan_backend::detail::validationMessageOrFallback;

TEST_CASE("effectiveValidationLayersEnabled forces validation on in a Debug build regardless of the request",
          "[vulkan_backend][validation_policy]") {
  REQUIRE(effectiveValidationLayersEnabled(/*isDebugBuild=*/true, /*requested=*/false));
  REQUIRE(effectiveValidationLayersEnabled(/*isDebugBuild=*/true, /*requested=*/true));
}

TEST_CASE("effectiveValidationLayersEnabled honors the caller's request in a Release build",
          "[vulkan_backend][validation_policy]") {
  REQUIRE_FALSE(effectiveValidationLayersEnabled(/*isDebugBuild=*/false, /*requested=*/false));
  REQUIRE(effectiveValidationLayersEnabled(/*isDebugBuild=*/false, /*requested=*/true));
}

TEST_CASE("isFatalValidationSeverity treats WARNING and ERROR as fatal", "[vulkan_backend][validation_policy]") {
  REQUIRE(isFatalValidationSeverity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT));
  REQUIRE(isFatalValidationSeverity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT));
}

TEST_CASE("isFatalValidationSeverity treats INFO and VERBOSE as non-fatal", "[vulkan_backend][validation_policy]") {
  REQUIRE_FALSE(isFatalValidationSeverity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT));
  REQUIRE_FALSE(isFatalValidationSeverity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT));
}

TEST_CASE("validationMessageOrFallback returns the callback's own message when present",
          "[vulkan_backend][validation_policy]") {
  VkDebugUtilsMessengerCallbackDataEXT callbackData{};
  callbackData.pMessage = "a real validation message";

  REQUIRE(std::string_view(validationMessageOrFallback(&callbackData)) == "a real validation message");
}

TEST_CASE("validationMessageOrFallback returns a stable, non-empty fallback for a null callbackData",
          "[vulkan_backend][validation_policy]") {
  const char* message = validationMessageOrFallback(nullptr);

  REQUIRE(message != nullptr);
  REQUIRE(std::string_view(message).size() > 0);
}

TEST_CASE("validationMessageOrFallback returns a stable, non-empty fallback for a null pMessage",
          "[vulkan_backend][validation_policy]") {
  VkDebugUtilsMessengerCallbackDataEXT callbackData{};
  callbackData.pMessage = nullptr;

  const char* message = validationMessageOrFallback(&callbackData);

  REQUIRE(message != nullptr);
  REQUIRE(std::string_view(message).size() > 0);
}

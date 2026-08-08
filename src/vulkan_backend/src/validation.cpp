#include "validation.h"

#include <cstdlib>

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

bool isFatalValidationSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept {
  return severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ||
         severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
}

const char* validationMessageOrFallback(const VkDebugUtilsMessengerCallbackDataEXT* callbackData) noexcept {
  if (callbackData == nullptr || callbackData->pMessage == nullptr) {
    return "Vulkan Validation Layers reported a WARNING/ERROR with no message text available";
  }
  return callbackData->pMessage;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                       const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                       void* /*userData*/) noexcept {
  if (isFatalValidationSeverity(severity)) {
    const char* message = validationMessageOrFallback(callbackData);
    // Reports through Core's existing, Accepted diagnostics/failure-
    // handler infrastructure (ADR-0009) -- ATLANTIS_LOG_FATAL logging,
    // debugger-break integration, and whatever handler is currently
    // installed all run here, unmodified. This call is NOT, on its own,
    // guaranteed to terminate -- see validation.h's documentation of what
    // ATLANTIS_CHECK_MSG actually guarantees.
    ATLANTIS_CHECK_MSG(false, message);
    // Structural fallback -- this module's own addition, not part of
    // ATLANTIS_CHECK_MSG's own contract, and not a change to ADR-0009.
    // reportFailure() is not [[noreturn]], and any AssertFailureHandler is
    // permitted to return (src/core/src/assert.cpp); the DEFAULT handler
    // already calls std::abort() internally, so in the ordinary case this
    // line is unreachable -- but if the currently-installed handler
    // returns instead of terminating, this call still guarantees the
    // process ends here. Not std::unreachable(): a handler returning is
    // legal per the existing API, not undefined behavior.
    std::abort();
  }
  return VK_FALSE;  // per the Vulkan spec: always VK_FALSE outside layer self-tests
}

}  // namespace atlantis::vulkan_backend::detail

#pragma once

#include <vulkan/vulkan_core.h>

// Private Vulkan Validation Layer enforcement policy. See
// plans/0003-rhi-vulkan-windowed-foundation.md Section 6: whenever
// validation layers are enabled, any WARNING/ERROR severity message must
// structurally fail the current process -- unconditionally, regardless of
// which AssertFailureHandler happens to be installed. Nothing here creates
// a VkInstance, a VkDevice, or a VkDebugUtilsMessengerEXT -- this file only
// declares the pure policy functions and the callback those future
// (Step 7) objects will be configured with.
namespace atlantis::vulkan_backend::detail {

#ifndef NDEBUG
inline constexpr bool IsDebugBuild = true;
#else
inline constexpr bool IsDebugBuild = false;
#endif

// The single place that turns a caller-requested value into the value
// Vulkan Backend actually uses. Parameterized on `isDebugBuild` (rather
// than reading IsDebugBuild internally) specifically so both branches --
// including the Debug-forces-true branch -- are exercised by a
// GPU-independent unit test without needing two separate build
// configurations. Future (Step 7) instance-creation code must call this
// function -- via effectiveValidationLayersEnabled(IsDebugBuild,
// params.enableValidationLayers) -- and use only its return value to
// decide whether validation layers are requested; it must never read
// DeviceCreateParams::enableValidationLayers directly for that decision.
[[nodiscard]] constexpr bool effectiveValidationLayersEnabled(bool isDebugBuild, bool requested) {
  return isDebugBuild || requested;
}

// WARNING/ERROR are fatal; INFO/VERBOSE are not. No configurable severity
// policy -- this classification is fixed.
[[nodiscard]] bool isFatalValidationSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept;

// Never dereferences a null callbackData or a null pMessage -- both are
// defensive, precautionary checks (the Vulkan specification does not
// document either as nullable for a conformant loader/layer, but this
// module does not assume every loader/layer implementation is fully
// conformant either). Always returns a valid, non-dangling
// const char* -- either callbackData->pMessage (owned by the Vulkan
// loader/layer for the duration of the callback) or a pointer to a
// static-storage-duration string literal; never a pointer into a
// temporary.
[[nodiscard]] const char* validationMessageOrFallback(
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData) noexcept;

// The Vulkan Debug Utils messenger callback. Not yet installed anywhere in
// this round -- Step 7 installs it via both a pNext-chained
// VkDebugUtilsMessengerCreateInfoEXT at vkCreateInstance time and an
// explicit VkDebugUtilsMessengerEXT, both with pUserData = nullptr (this
// callback needs no per-instance state; userData is ignored here and must
// stay nullptr wherever this callback is installed). Stateless and
// noexcept: no global mutable state, no atomic/mutex/thread, calls no
// Vulkan API, throws nothing.
//
// On INFO/VERBOSE: returns VK_FALSE, reports nothing.
// On WARNING/ERROR: obtains a safe message via validationMessageOrFallback(),
// then calls ATLANTIS_CHECK_MSG(false, message) -- Core's existing,
// Accepted diagnostics path (ADR-0009): ATLANTIS_LOG_FATAL logging,
// debugger-break integration, and whatever AssertFailureHandler is
// currently installed all run there, unmodified. That call alone does
// NOT guarantee termination -- reportFailure() is not [[noreturn]], and
// any AssertFailureHandler is permitted to return (see
// src/core/src/assert.cpp). An explicit, unconditional std::abort()
// immediately follows it with no intervening return or conditional --
// this is this module's own local reinforcement of the fatal invariant
// above, not a change to ATLANTIS_CHECK_MSG/ADR-0009 and not a new public
// assertion mechanism (confirmed at Human Review, per
// plans/0003-rhi-vulkan-windowed-foundation.md Section 6/Consistency
// Review item 17). In the ordinary case (the default failure handler,
// which already calls std::abort() internally) this std::abort() is
// unreachable; it exists for the case where a replacement handler
// returns instead of terminating -- a legal, existing behavior of Core's
// API, not undefined behavior, so this is not std::unreachable(). The
// callback's own return value (always VK_FALSE, per the Vulkan
// specification's recommendation outside layer self-tests) is not itself
// what makes WARNING/ERROR fatal -- the std::abort() above is.
VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT types,
                                                       const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                       void* userData) noexcept;

}  // namespace atlantis::vulkan_backend::detail

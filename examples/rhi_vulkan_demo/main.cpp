#include <atlantis/log.h>
#include <atlantis/platform/platform.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <cstdlib>
#include <memory>
#include <variant>

// Spec 0003's non-shipping verification composition (see
// specs/0003-rhi-vulkan-windowed-foundation.md's Non-Goals and
// plans/0003-rhi-vulkan-windowed-foundation.md Section 11). This is NOT
// the Atlantis Runtime module and does not preview its future
// architecture -- Runtime's own responsibilities (owning Platform across
// the application's full lifetime, general frame-loop policy, Android
// pause/resume handling) remain a separate future spec's scope. This demo
// renders nothing: the window staying blank throughout is expected. It
// exists solely to exercise Presentation's non-frame lifecycle
// (construction, resize-driven recreation, zero-extent deferral,
// metadata, destruction) end-to-end, interactively, per ADR-0016 -- no
// acquireNextTarget()-shaped call, no present() call, and no command
// buffer of any kind exists anywhere in this file.

namespace {

[[nodiscard]] const char* platformErrorCodeToString(atlantis::platform::PlatformErrorCode code) {
  using atlantis::platform::PlatformErrorCode;
  switch (code) {
    case PlatformErrorCode::WindowClassRegistrationFailed:
      return "WindowClassRegistrationFailed";
    case PlatformErrorCode::WindowCreationFailed:
      return "WindowCreationFailed";
  }
  return "(unrecognized PlatformErrorCode)";
}

[[nodiscard]] const char* deviceCreateErrorToString(atlantis::vulkan_backend::DeviceCreateError error) {
  using atlantis::vulkan_backend::DeviceCreateError;
  switch (error) {
    case DeviceCreateError::InstanceCreationFailed:
      return "InstanceCreationFailed";
    case DeviceCreateError::ValidationLayerUnavailable:
      return "ValidationLayerUnavailable";
    case DeviceCreateError::NoSuitablePhysicalDevice:
      return "NoSuitablePhysicalDevice";
    case DeviceCreateError::DeviceCreationFailed:
      return "DeviceCreationFailed";
  }
  return "(unrecognized DeviceCreateError)";
}

[[nodiscard]] const char* presentationCreateErrorToString(atlantis::vulkan_backend::PresentationCreateError error) {
  using atlantis::vulkan_backend::PresentationCreateError;
  switch (error) {
    case PresentationCreateError::SurfaceCreationFailed:
      return "SurfaceCreationFailed";
    case PresentationCreateError::UnsupportedDevice:
      return "UnsupportedDevice";
  }
  return "(unrecognized PresentationCreateError)";
}

[[nodiscard]] const char* presentationErrorToString(atlantis::rhi::PresentationError error) {
  using atlantis::rhi::PresentationError;
  switch (error) {
    case PresentationError::SurfaceLost:
      return "SurfaceLost";
    case PresentationError::SwapchainCreationFailed:
      return "SwapchainCreationFailed";
    case PresentationError::DeviceLost:
      return "DeviceLost";
    case PresentationError::Unknown:
      return "Unknown";
  }
  return "(unrecognized PresentationError)";
}

[[nodiscard]] const char* formatToString(atlantis::rhi::Format format) {
  using atlantis::rhi::Format;
  switch (format) {
    case Format::Unknown:
      return "Unknown";
    case Format::Bgra8Unorm:
      return "Bgra8Unorm";
    case Format::Bgra8Srgb:
      return "Bgra8Srgb";
    case Format::Rgba8Unorm:
      return "Rgba8Unorm";
    case Format::Rgba8Srgb:
      return "Rgba8Srgb";
  }
  return "(unrecognized Format)";
}

}  // namespace

int main() {
  namespace platform = atlantis::platform;
  namespace rhi = atlantis::rhi;
  namespace vulkan_backend = atlantis::vulkan_backend;

  ATLANTIS_LOG_INFO("Atlantis RHI Vulkan demo starting");
  ATLANTIS_LOG_INFO(
      "This is Spec 0003's non-shipping verification composition, not the Atlantis Runtime module and not a "
      "preview of it. It renders nothing -- the window staying blank is expected. It verifies Presentation's "
      "non-frame lifecycle only.");

  auto initResult = platform::initialize();
  if (initResult.isErr()) {
    const auto& error = initResult.error();
    ATLANTIS_LOG_ERROR("platform::initialize() failed: {} (nativeErrorCode={})",
                        platformErrorCodeToString(error.code), error.nativeErrorCode);
    return EXIT_FAILURE;
  }
  ATLANTIS_LOG_INFO("Platform initialized");

  // Validation Layers explicitly requested regardless of Debug/Release --
  // any WARNING/ERROR the callback observes anywhere below aborts this
  // process (validation.cpp); no replacement failure handler is installed
  // anywhere in this file, so this demo running to completion and exiting
  // 0 is itself the validation-clean signal.
  auto deviceResult = vulkan_backend::createDevice(
      {.applicationName = "Atlantis RHI Vulkan Demo", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    ATLANTIS_LOG_ERROR("createDevice() failed: {}", deviceCreateErrorToString(deviceResult.error()));
    platform::shutdown();
    static_cast<void>(platform::processEvents());
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());
  ATLANTIS_LOG_INFO("Vulkan Device created (Validation Layers requested)");

  // Constructed once SurfaceCreated arrives below. device must outlive
  // presentation -- enforced here by declaration order plus the explicit
  // presentation.reset() before device.reset() in every cleanup path
  // below (never the reverse).
  std::unique_ptr<rhi::Presentation> presentation;
  bool failed = false;

  while (!platform::shouldQuit()) {
    bool closeRequested = false;

    // The span processEvents() returns here is valid only until the next
    // processEvents()/shutdown() call -- shutdown() is therefore never
    // called anywhere inside this for loop, only after it has finished
    // iterating this batch.
    for (const auto& event : platform::processEvents()) {
      if (const auto* created = std::get_if<platform::SurfaceCreated>(&event)) {
        if (presentation) {
          ATLANTIS_LOG_ERROR(
              "SurfaceCreated observed while a Presentation already exists -- unexpected lifecycle event");
          failed = true;
          continue;
        }
        auto presentationResult = vulkan_backend::createPresentation(*device, created->handle);
        if (presentationResult.isErr()) {
          ATLANTIS_LOG_ERROR("createPresentation() failed: {}",
                              presentationCreateErrorToString(presentationResult.error()));
          failed = true;
          continue;
        }
        presentation = std::move(presentationResult.value());
        ATLANTIS_LOG_INFO("Presentation created (surface only, no swapchain yet)");
      } else if (const auto* resize = std::get_if<platform::WindowResize>(&event)) {
        ATLANTIS_LOG_INFO("WindowResize: logical={}x{} framebuffer={}x{}", resize->logical.width,
                           resize->logical.height, resize->framebuffer.width, resize->framebuffer.height);
        if (!presentation) {
          ATLANTIS_LOG_INFO("WindowResize observed before Presentation exists -- ignoring (waiting for "
                             "SurfaceCreated, which Windows Platform always delivers first)");
          continue;
        }

        const rhi::Extent2D framebufferExtent{resize->framebuffer.width, resize->framebuffer.height};
        presentation->notifyResized(framebufferExtent);
        auto recreateResult = presentation->recreateIfNeeded();
        if (recreateResult.isErr()) {
          ATLANTIS_LOG_ERROR("recreateIfNeeded() failed: {}", presentationErrorToString(recreateResult.error()));
          failed = true;
          continue;
        }

        const rhi::SwapchainMetadata metadata = presentation->metadata();
        if (framebufferExtent.isZero()) {
          // ADR-0016: a swapchain created at a prior non-zero extent is
          // left untouched (not eagerly released) once extent becomes
          // zero -- metadata below still reflects that prior swapchain,
          // or the no-swapchain default if none was ever created yet.
          ATLANTIS_LOG_INFO(
              "Zero framebuffer extent -- swapchain (re)creation deferred (ADR-0016); retained metadata: "
              "imageCount={} format={} extent={}x{}",
              metadata.imageCount, formatToString(metadata.format), metadata.extent.width, metadata.extent.height);
        } else if (metadata.imageCount == 0 || metadata.format == rhi::Format::Unknown || metadata.extent.isZero()) {
          ATLANTIS_LOG_ERROR(
              "recreateIfNeeded() returned Ok for a non-zero extent but metadata is still at its default state");
          failed = true;
        } else {
          ATLANTIS_LOG_INFO("Swapchain (re)created: imageCount={} format={} extent={}x{}", metadata.imageCount,
                             formatToString(metadata.format), metadata.extent.width, metadata.extent.height);
        }
      } else if (std::holds_alternative<platform::WindowCloseRequested>(event)) {
        ATLANTIS_LOG_INFO("WindowCloseRequested");
        closeRequested = true;
      } else if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
        if (presentation) {
          ATLANTIS_LOG_ERROR("SurfaceDestroyed observed while a Presentation still exists -- it must already have "
                              "been destroyed by this point");
          failed = true;
          continue;
        }
        ATLANTIS_LOG_INFO("SurfaceDestroyed");
      } else if (std::holds_alternative<platform::Quit>(event)) {
        ATLANTIS_LOG_INFO("Quit");
      } else if (std::holds_alternative<platform::FocusGained>(event)) {
        ATLANTIS_LOG_INFO("FocusGained");
      } else if (std::holds_alternative<platform::FocusLost>(event)) {
        ATLANTIS_LOG_INFO("FocusLost");
      } else if (std::holds_alternative<platform::ApplicationPause>(event)) {
        ATLANTIS_LOG_INFO("ApplicationPause");
      } else if (std::holds_alternative<platform::ApplicationResume>(event)) {
        ATLANTIS_LOG_INFO("ApplicationResume");
      }
    }

    // Cleanup happens only after the for loop above has fully finished
    // iterating this call's processEvents() span. Presentation is
    // destroyed before Device, and Device before Platform shutdown --
    // Vulkan objects never outlive the native window they were built
    // from.
    if (failed || closeRequested) {
      presentation.reset();
      device.reset();
      platform::shutdown();
    }
  }

  // shutdown() above already invalidated the previous processEvents()
  // span. Calling processEvents() once more here remains legal
  // specifically to observe the final {SurfaceDestroyed, Quit} batch
  // shutdown() synchronously produced.
  for (const auto& event : platform::processEvents()) {
    if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
      ATLANTIS_LOG_INFO("SurfaceDestroyed");
    } else if (std::holds_alternative<platform::Quit>(event)) {
      ATLANTIS_LOG_INFO("Quit");
    }
  }

  if (failed) {
    ATLANTIS_LOG_ERROR("Atlantis RHI Vulkan demo finished with failures");
    return EXIT_FAILURE;
  }

  ATLANTIS_LOG_INFO("Atlantis RHI Vulkan demo finished");
  return EXIT_SUCCESS;
}

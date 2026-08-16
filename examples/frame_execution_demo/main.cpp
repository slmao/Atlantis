#include <atlantis/log.h>
#include <atlantis/platform/platform.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <cstdlib>
#include <memory>
#include <variant>

// Spec 0006's non-shipping verification composition (see
// specs/0006-rhi-render-graph-frame-execution-foundation.md's Testing &
// Verification Plan and
// plans/0006-rhi-render-graph-frame-execution-foundation.md Section 13).
// This is NOT the Atlantis Runtime module and does not preview its future
// architecture -- same disclaimer as examples/rhi_vulkan_demo, which
// remains Spec 0003's own, unmodified, still-renders-nothing artifact.
// Unlike that demo, THIS one renders: every frame, it acquires a
// RenderTarget, builds and compiles a one-pass RenderGraph that clears
// it, executes that graph (recording only -- RenderGraph never submits
// or presents), submits the recorded CommandList, and presents. All GPU
// work goes through RenderGraph; no direct-submission bypass exists
// anywhere in this file.

namespace {

using atlantis::render_graph::execute;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;

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

// Builds a single-pass RenderGraph that clears whatever RenderTarget it
// is bound to, compiles it, and drives it through render_graph::execute()
// into commandList. Returns false (logging why) on a compile failure --
// not expected for this fixed, single-pass graph shape, but checked
// rather than assumed.
[[nodiscard]] bool recordOneClearPass(atlantis::rhi::RenderTarget& target, atlantis::rhi::CommandList& commandList) {
  RenderGraphBuilder builder;
  const auto resource = builder.declareResource("frame-target");
  const auto clearPass = builder.declarePass("clear");
  builder.writes(clearPass, resource, atlantis::rhi::ResourceState::ColorAttachmentWrite);
  builder.setExecute(clearPass, [&target](atlantis::rhi::CommandList& cmd) {
    cmd.clearColor(target, atlantis::rhi::ClearColorValue{0.05f, 0.2f, 0.35f, 1.0f});
  });

  auto compileResult = builder.compile();
  if (compileResult.isErr()) {
    ATLANTIS_LOG_ERROR("RenderGraph compile() failed unexpectedly for this demo's fixed one-pass graph");
    return false;
  }

  const std::vector<ResourceBinding> bindings{{.resource = compileResult.value().resourceAt(0),
                                                .target = &target,
                                                .finalState = atlantis::rhi::ResourceState::PresentSource}};
  execute(compileResult.value(), bindings, commandList);
  return true;
}

}  // namespace

int main() {
  namespace platform = atlantis::platform;
  namespace rhi = atlantis::rhi;
  namespace vulkan_backend = atlantis::vulkan_backend;

  atlantis::log::setMinLevel(atlantis::LogLevel::Info);

  ATLANTIS_LOG_INFO("Atlantis Frame Execution demo starting");
  ATLANTIS_LOG_INFO(
      "This is Spec 0006's non-shipping verification composition, not the Atlantis Runtime module and not a "
      "preview of it. Every frame acquires a RenderTarget, builds/compiles/executes a one-pass RenderGraph that "
      "clears it, submits, and presents.");

  auto initResult = platform::initialize();
  if (initResult.isErr()) {
    const auto& error = initResult.error();
    ATLANTIS_LOG_ERROR("platform::initialize() failed: {} (nativeErrorCode={})",
                        platformErrorCodeToString(error.code), error.nativeErrorCode);
    return EXIT_FAILURE;
  }
  ATLANTIS_LOG_INFO("Platform initialized");

  auto deviceResult = vulkan_backend::createDevice(
      {.applicationName = "Atlantis Frame Execution Demo", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    ATLANTIS_LOG_ERROR("createDevice() failed: {}", deviceCreateErrorToString(deviceResult.error()));
    platform::shutdown();
    static_cast<void>(platform::processEvents());
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());
  ATLANTIS_LOG_INFO("Vulkan Device created (Validation Layers requested)");

  std::unique_ptr<rhi::Presentation> presentation;
  bool failed = false;
  std::uint64_t framesDrawn = 0;

  while (!platform::shouldQuit()) {
    bool closeRequested = false;

    for (const auto& event : platform::processEvents()) {
      if (const auto* created = std::get_if<platform::SurfaceCreated>(&event)) {
        if (presentation) {
          ATLANTIS_LOG_ERROR("SurfaceCreated observed while a Presentation already exists");
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
        ATLANTIS_LOG_INFO("Presentation created");
      } else if (const auto* resize = std::get_if<platform::WindowResize>(&event)) {
        ATLANTIS_LOG_DEBUG("WindowResize: logical={}x{} framebuffer={}x{}", resize->logical.width,
                            resize->logical.height, resize->framebuffer.width, resize->framebuffer.height);
        if (!presentation) {
          ATLANTIS_LOG_INFO("WindowResize observed before Presentation exists -- ignoring");
          continue;
        }
        // acquireNextTarget() folds recreateIfNeeded()'s recreation
        // timing into itself (ADR-0019) -- notifyResized() is all this
        // handler needs to do; the next frame's acquire recreates.
        const rhi::Extent2D framebufferExtent{resize->framebuffer.width, resize->framebuffer.height};
        presentation->notifyResized(framebufferExtent);
      } else if (std::holds_alternative<platform::WindowCloseRequested>(event)) {
        ATLANTIS_LOG_INFO("WindowCloseRequested");
        closeRequested = true;
      } else if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
        if (presentation) {
          ATLANTIS_LOG_ERROR("SurfaceDestroyed observed while a Presentation still exists");
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

    // One frame, per iteration, once a Presentation exists and nothing
    // has failed/requested close this iteration.
    if (presentation && !failed && !closeRequested) {
      auto acquireResult = presentation->acquireNextTarget();
      if (acquireResult.isErr()) {
        ATLANTIS_LOG_ERROR("acquireNextTarget() failed: {}", presentationErrorToString(acquireResult.error()));
        failed = true;
      } else if (acquireResult.value() == nullptr) {
        // Nothing to draw this frame (zero extent, or an out-of-date
        // swapchain deferred to the next call) -- not an error.
        ATLANTIS_LOG_DEBUG("acquireNextTarget(): nothing to draw this frame");
      } else {
        std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

        auto commandListResult = device->createCommandList();
        if (commandListResult.isErr()) {
          ATLANTIS_LOG_ERROR("createCommandList() failed");
          failed = true;
        } else {
          std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

          if (!recordOneClearPass(*target, *commandList)) {
            failed = true;
          } else {
            auto submitResult = device->submit(std::move(commandList), *target);
            if (submitResult.isErr()) {
              ATLANTIS_LOG_ERROR("submit() failed");
              failed = true;
            } else {
              auto presentResult = presentation->present(std::move(target), std::move(submitResult.value()));
              if (presentResult.isErr()) {
                ATLANTIS_LOG_ERROR("present() failed: {}", presentationErrorToString(presentResult.error()));
                failed = true;
              } else {
                ++framesDrawn;
                if (framesDrawn % 120 == 0) {
                  ATLANTIS_LOG_DEBUG("Drawn {} frames so far", framesDrawn);
                }
              }
            }
          }
        }
      }
    }

    if (failed || closeRequested) {
      // Required before destroying Presentation/Device whenever a
      // RenderTarget may have been acquired or a submission made
      // (ADR-0019) -- drains any outstanding GPU work, including on a
      // mid-frame or submit-then-exit path.
      if (device) {
        const auto idleResult = device->waitIdle();
        if (idleResult.isErr()) {
          ATLANTIS_LOG_ERROR("waitIdle() failed during shutdown");
        }
      }
      presentation.reset();
      device.reset();
      platform::shutdown();
    }
  }

  for (const auto& event : platform::processEvents()) {
    if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
      ATLANTIS_LOG_INFO("SurfaceDestroyed");
    } else if (std::holds_alternative<platform::Quit>(event)) {
      ATLANTIS_LOG_INFO("Quit");
    }
  }

  if (failed) {
    ATLANTIS_LOG_ERROR("Atlantis Frame Execution demo finished with failures");
    return EXIT_FAILURE;
  }

  ATLANTIS_LOG_INFO("Atlantis Frame Execution demo finished after drawing {} frames", framesDrawn);
  return EXIT_SUCCESS;
}

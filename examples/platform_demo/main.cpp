#include <atlantis/log.h>
#include <atlantis/platform/clock.h>
#include <atlantis/platform/platform.h>

#include <chrono>
#include <cstdlib>
#include <variant>

namespace {

const char* platformErrorCodeToString(atlantis::platform::PlatformErrorCode code) {
  using atlantis::platform::PlatformErrorCode;
  switch (code) {
    case PlatformErrorCode::WindowClassRegistrationFailed:
      return "WindowClassRegistrationFailed";
    case PlatformErrorCode::WindowCreationFailed:
      return "WindowCreationFailed";
  }
  return "Unknown";
}

void logEvent(const atlantis::platform::PlatformEvent& event) {
  using atlantis::platform::ApplicationPause;
  using atlantis::platform::ApplicationResume;
  using atlantis::platform::FocusGained;
  using atlantis::platform::FocusLost;
  using atlantis::platform::Quit;
  using atlantis::platform::SurfaceCreated;
  using atlantis::platform::SurfaceDestroyed;
  using atlantis::platform::WindowCloseRequested;
  using atlantis::platform::WindowResize;

  if (const auto* resize = std::get_if<WindowResize>(&event)) {
    ATLANTIS_LOG_INFO("WindowResize: logical={}x{} framebuffer={}x{}", resize->logical.width,
                       resize->logical.height, resize->framebuffer.width, resize->framebuffer.height);
  } else if (std::holds_alternative<WindowCloseRequested>(event)) {
    ATLANTIS_LOG_INFO("WindowCloseRequested");
  } else if (std::holds_alternative<FocusGained>(event)) {
    ATLANTIS_LOG_INFO("FocusGained");
  } else if (std::holds_alternative<FocusLost>(event)) {
    ATLANTIS_LOG_INFO("FocusLost");
  } else if (std::holds_alternative<ApplicationPause>(event)) {
    ATLANTIS_LOG_INFO("ApplicationPause");
  } else if (std::holds_alternative<ApplicationResume>(event)) {
    ATLANTIS_LOG_INFO("ApplicationResume");
  } else if (std::holds_alternative<SurfaceCreated>(event)) {
    // Per ADR-0011, SurfaceCreated's NativeWindowHandle payload
    // (value0/value1) is opaque and interpreted only by the active
    // graphics backend's private WSI boundary. This demo has no such
    // boundary (no Vulkan/RHI here) and must not read those fields --
    // logging only that the event occurred.
    ATLANTIS_LOG_INFO("SurfaceCreated");
  } else if (std::holds_alternative<SurfaceDestroyed>(event)) {
    ATLANTIS_LOG_INFO("SurfaceDestroyed");
  } else if (std::holds_alternative<Quit>(event)) {
    ATLANTIS_LOG_INFO("Quit");
  }
}

}  // namespace

int main() {
  namespace platform = atlantis::platform;

  ATLANTIS_LOG_INFO("Atlantis Platform demo starting");

  auto initResult = platform::initialize();
  if (initResult.isErr()) {
    const auto& error = initResult.error();
    ATLANTIS_LOG_ERROR("Platform::initialize() failed: {} (nativeErrorCode={})",
                        platformErrorCodeToString(error.code), error.nativeErrorCode);
    return EXIT_FAILURE;
  }
  ATLANTIS_LOG_INFO("Platform initialized");

  const auto loopStart = platform::monotonicNow();
  auto lastLoggedElapsed = platform::TimePoint::duration::zero();

  while (!platform::shouldQuit()) {
    bool closeRequested = false;
    for (const auto& event : platform::processEvents()) {
      logEvent(event);
      if (std::holds_alternative<platform::WindowCloseRequested>(event)) {
        closeRequested = true;
      }
    }

    // monotonicNow() is queried every iteration to prove the Platform
    // timing API is usable from a real event loop. The resulting log line
    // is throttled to roughly once per second so this deliberately
    // unpaced loop (no sleep/frame-pacing is added, per this task's
    // scope) doesn't flood the console with a line per spin.
    const auto elapsed = platform::monotonicNow() - loopStart;
    if (elapsed - lastLoggedElapsed >= std::chrono::seconds(1)) {
      const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
      ATLANTIS_LOG_INFO("Elapsed since start: {} ms", elapsedMs);
      lastLoggedElapsed = elapsed;
    }

    // shutdown() is called only after the for loop above has finished
    // iterating this call's processEvents() span -- calling it
    // mid-iteration would invalidate the span being iterated.
    if (closeRequested) {
      platform::shutdown();
    }
  }

  // shutdown() above already invalidated the previous processEvents()
  // span. Calling processEvents() once more here remains legal
  // specifically to observe the final {SurfaceDestroyed, Quit} batch
  // shutdown() synchronously produced.
  for (const auto& event : platform::processEvents()) {
    logEvent(event);
  }

  ATLANTIS_LOG_INFO("Atlantis Platform demo finished");
  return EXIT_SUCCESS;
}

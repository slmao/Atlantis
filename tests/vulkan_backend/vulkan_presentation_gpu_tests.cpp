#include <atlantis/platform/platform.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <catch2/catch_test_macros.hpp>

// GPU-required, windowed integration coverage for Presentation's non-frame
// lifecycle (Spec 0003 / ADR-0016), per
// plans/0003-rhi-vulkan-windowed-foundation.md Section 8/Implementation
// Order Step 10. Windows-only, real Vulkan device, real window -- see
// tests/platform/windows_platform_smoke_tests.cpp for the sibling Windows
// test boundary this file mirrors (Platform's own lifecycle/event
// contract vs. this file's RHI/Vulkan Backend Presentation contract).
// Drives Presentation exclusively through Atlantis's public API
// (atlantis::vulkan_backend::createDevice()/createPresentation(),
// atlantis::rhi::Presentation's public methods) -- no Vulkan header, no
// Vk* type, and no direct Vulkan call appears anywhere in this file.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <memory>
#include <span>
#include <variant>

namespace {

using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::Presentation;
using atlantis::rhi::SwapchainMetadata;
using atlantis::vulkan_backend::DeviceCreateError;
using atlantis::vulkan_backend::PresentationCreateError;

// Phase 1 does not support re-initializing Platform after shutdown()
// within one process (mirrors tests/platform/windows_platform_smoke_tests.cpp's
// own PlatformLifecycleGuard), so this file's single TEST_CASE shares one
// initialize()->shutdown() lifecycle rather than splitting into Catch2
// SECTIONs, which would re-run the test body -- and therefore
// initialize() -- once per SECTION. Every Presentation/Device this test
// owns is declared in a scope nested inside this guard's own scope, so a
// failing REQUIRE unwinds them (via ordinary C++ destructor order)
// before this guard's destructor ever calls shutdown() -- the window is
// never destroyed while a Presentation or Device the test still holds
// could reference it.
class PlatformLifecycleGuard {
 public:
  PlatformLifecycleGuard() = default;
  ~PlatformLifecycleGuard() {
    if (!shutDown_) {
      atlantis::platform::shutdown();
    }
  }

  PlatformLifecycleGuard(const PlatformLifecycleGuard&) = delete;
  PlatformLifecycleGuard& operator=(const PlatformLifecycleGuard&) = delete;

  void markShutDown() { shutDown_ = true; }

 private:
  bool shutDown_ = false;
};

[[nodiscard]] const char* deviceCreateErrorToString(DeviceCreateError error) {
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

[[nodiscard]] const char* presentationCreateErrorToString(PresentationCreateError error) {
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

[[nodiscard]] const atlantis::platform::WindowResize* findNonZeroFramebufferResize(
    std::span<const atlantis::platform::PlatformEvent> events) {
  for (const auto& event : events) {
    if (const auto* resize = std::get_if<atlantis::platform::WindowResize>(&event)) {
      if (!resize->framebuffer.isZero()) {
        return resize;
      }
    }
  }
  return nullptr;
}

[[nodiscard]] bool containsZeroFramebufferResize(std::span<const atlantis::platform::PlatformEvent> events) {
  for (const auto& event : events) {
    if (const auto* resize = std::get_if<atlantis::platform::WindowResize>(&event)) {
      if (resize->framebuffer.isZero()) {
        return true;
      }
    }
  }
  return false;
}

// A short, explicitly-bounded drain loop -- not an unbounded wait and not
// a busy-spin with sleeps. Win32 dispatches SetWindowPos/ShowWindow
// synchronously in practice, so the matching event is normally already in
// the very next processEvents() batch; the bound exists only as a
// defensive cap, not as this function's primary synchronization
// mechanism.
constexpr int kMaxEventDrainIterations = 8;

[[nodiscard]] Extent2D resizeWindowAndReadFramebufferExtent(HWND hwnd, int width, int height) {
  SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
  for (int i = 0; i < kMaxEventDrainIterations; ++i) {
    const auto events = atlantis::platform::processEvents();
    if (const auto* resize = findNonZeroFramebufferResize(events)) {
      // atlantis::platform::WindowExtent and atlantis::rhi::Extent2D are
      // distinct types with the same shape (Platform and RHI stay
      // decoupled) -- an explicit field-wise conversion at this Runtime-
      // equivalent boundary is expected, not a workaround.
      return Extent2D{resize->framebuffer.width, resize->framebuffer.height};
    }
  }
  FAIL("Timed out waiting for a non-zero WindowResize event after SetWindowPos(" << width << "x" << height << ")");
  return Extent2D{};
}

void minimizeWindowAndObserveZeroExtent(HWND hwnd) {
  ShowWindow(hwnd, SW_MINIMIZE);
  for (int i = 0; i < kMaxEventDrainIterations; ++i) {
    const auto events = atlantis::platform::processEvents();
    if (containsZeroFramebufferResize(events)) {
      return;
    }
  }
  FAIL("Timed out waiting for a zero-extent WindowResize event after ShowWindow(SW_MINIMIZE)");
}

[[nodiscard]] Extent2D restoreWindowAndReadFramebufferExtent(HWND hwnd) {
  ShowWindow(hwnd, SW_RESTORE);
  for (int i = 0; i < kMaxEventDrainIterations; ++i) {
    const auto events = atlantis::platform::processEvents();
    if (const auto* resize = findNonZeroFramebufferResize(events)) {
      // atlantis::platform::WindowExtent and atlantis::rhi::Extent2D are
      // distinct types with the same shape (Platform and RHI stay
      // decoupled) -- an explicit field-wise conversion at this Runtime-
      // equivalent boundary is expected, not a workaround.
      return Extent2D{resize->framebuffer.width, resize->framebuffer.height};
    }
  }
  FAIL("Timed out waiting for a non-zero WindowResize event after ShowWindow(SW_RESTORE)");
  return Extent2D{};
}

void requireMetadataUnchanged(const SwapchainMetadata& before, const SwapchainMetadata& after) {
  REQUIRE(after.imageCount == before.imageCount);
  REQUIRE(after.format == before.format);
  REQUIRE(after.extent == before.extent);
}

}  // namespace

TEST_CASE(
    "VulkanPresentation non-frame lifecycle: construction, zero-extent skip, resize-driven recreation, "
    "minimize/restore, and destruction at multiple points",
    "[vulkan_backend][presentation][gpu][integration]") {
  using atlantis::platform::PlatformEvent;
  using atlantis::platform::SurfaceCreated;
  using atlantis::vulkan_backend::createDevice;
  using atlantis::vulkan_backend::createPresentation;
  using atlantis::vulkan_backend::DeviceCreateParams;

  const auto initResult = atlantis::platform::initialize();
  if (initResult.isErr()) {
    FAIL("platform::initialize() failed");
  }

  // Constructed only after a successful initialize(), so its destructor
  // never calls shutdown() on a Platform that was never initialized.
  PlatformLifecycleGuard platformGuard;

  const auto initialBatch = atlantis::platform::processEvents();
  REQUIRE(initialBatch.size() >= 1);
  REQUIRE(std::holds_alternative<SurfaceCreated>(initialBatch[0]));
  const atlantis::platform::NativeWindowHandle windowHandle = std::get<SurfaceCreated>(initialBatch[0]).handle;

  const HWND hwnd = reinterpret_cast<HWND>(windowHandle.value0);
  REQUIRE(hwnd != nullptr);
  REQUIRE(IsWindow(hwnd));

  // Validation layers explicitly requested regardless of Debug/Release --
  // any WARNING/ERROR the callback observes anywhere below aborts this
  // process (validation.cpp), which CTest reports as a failed test. No
  // replacement failure handler is installed anywhere in this file.
  auto deviceResult =
      createDevice(DeviceCreateParams{.applicationName = "Atlantis Vulkan GPU Tests", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    FAIL("createDevice() failed: " << deviceCreateErrorToString(deviceResult.error()));
  }
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  // --- Destruction point 1: surface-only, immediately after construction. ---
  // No notifyResized()/recreateIfNeeded() call -- covers surface creation,
  // the concrete-surface support check, and destruction of a Presentation
  // that never created a swapchain.
  {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed: " << presentationCreateErrorToString(presentationResult.error()));
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());
    // Destroyed at the end of this scope.
  }

  // --- Destruction point 2: immediately after the first successful swapchain creation. ---
  // Also covers the initial zero-extent structural skip and its
  // idempotence before the first real resize.
  {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed: " << presentationCreateErrorToString(presentationResult.error()));
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    presentation->notifyResized(Extent2D{0, 0});
    auto skipResult = presentation->recreateIfNeeded();
    if (skipResult.isErr()) {
      FAIL("recreateIfNeeded() at zero extent failed: " << presentationErrorToString(skipResult.error()));
    }
    const SwapchainMetadata afterFirstSkip = presentation->metadata();
    REQUIRE(afterFirstSkip.imageCount == 0);
    REQUIRE(afterFirstSkip.format == Format::Unknown);
    REQUIRE(afterFirstSkip.extent.isZero());

    // Repeated at zero extent: still Ok, metadata unchanged. This
    // corroborates the structural Skip guarantee (already verified by
    // code inspection and the GPU-independent decideRecreateAction()
    // tests) against a real device -- it does not re-derive it, and does
    // not probe any private Vulkan call count.
    auto skipResult2 = presentation->recreateIfNeeded();
    if (skipResult2.isErr()) {
      FAIL("second recreateIfNeeded() at zero extent failed: " << presentationErrorToString(skipResult2.error()));
    }
    requireMetadataUnchanged(afterFirstSkip, presentation->metadata());

    const Extent2D framebuffer = resizeWindowAndReadFramebufferExtent(hwnd, 400, 300);
    presentation->notifyResized(framebuffer);
    auto recreateResult = presentation->recreateIfNeeded();
    if (recreateResult.isErr()) {
      FAIL("recreateIfNeeded() after first non-zero resize failed: "
           << presentationErrorToString(recreateResult.error()));
    }
    const SwapchainMetadata afterFirstCreate = presentation->metadata();
    REQUIRE(afterFirstCreate.imageCount > 0);
    REQUIRE(afterFirstCreate.format != Format::Unknown);
    REQUIRE_FALSE(afterFirstCreate.extent.isZero());
    REQUIRE(afterFirstCreate.extent == framebuffer);

    // Destroyed at the end of this scope, right after its first
    // successful swapchain creation.
  }

  // --- Destruction point 3: after multiple resize/recreation cycles. ---
  {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed: " << presentationCreateErrorToString(presentationResult.error()));
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    const Extent2D framebufferA = resizeWindowAndReadFramebufferExtent(hwnd, 500, 350);
    presentation->notifyResized(framebufferA);
    auto recreateA = presentation->recreateIfNeeded();
    if (recreateA.isErr()) {
      FAIL("recreateIfNeeded() after resize A failed: " << presentationErrorToString(recreateA.error()));
    }
    const SwapchainMetadata metadataA = presentation->metadata();
    REQUIRE(metadataA.imageCount > 0);
    REQUIRE(metadataA.format != Format::Unknown);
    REQUIRE(metadataA.extent == framebufferA);

    // NoOp idempotence: no notifyResized() call between these two
    // recreateIfNeeded() calls.
    auto noOpResult = presentation->recreateIfNeeded();
    if (noOpResult.isErr()) {
      FAIL("NoOp recreateIfNeeded() failed: " << presentationErrorToString(noOpResult.error()));
    }
    requireMetadataUnchanged(metadataA, presentation->metadata());

    // A second, different non-zero size -- distinct from framebufferA so
    // this genuinely exercises a second recreation rather than
    // coincidentally matching the first.
    const Extent2D framebufferB = resizeWindowAndReadFramebufferExtent(hwnd, 350, 500);
    presentation->notifyResized(framebufferB);
    auto recreateB = presentation->recreateIfNeeded();
    if (recreateB.isErr()) {
      FAIL("recreateIfNeeded() after resize B failed: " << presentationErrorToString(recreateB.error()));
    }
    const SwapchainMetadata metadataB = presentation->metadata();
    REQUIRE_FALSE(metadataB.extent.isZero());
    REQUIRE(metadataB.extent == framebufferB);
    REQUIRE(metadataB.imageCount > 0);
    REQUIRE(metadataB.format != Format::Unknown);

    auto noOpResult2 = presentation->recreateIfNeeded();
    if (noOpResult2.isErr()) {
      FAIL("second NoOp recreateIfNeeded() failed: " << presentationErrorToString(noOpResult2.error()));
    }
    requireMetadataUnchanged(metadataB, presentation->metadata());

    // Destroyed at the end of this scope, after multiple resize/recreation
    // cycles.
  }

  // --- Destruction point 4: after a minimize/restore cycle. ---
  {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed: " << presentationCreateErrorToString(presentationResult.error()));
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    const Extent2D framebufferInitial = resizeWindowAndReadFramebufferExtent(hwnd, 450, 320);
    presentation->notifyResized(framebufferInitial);
    auto recreateInitial = presentation->recreateIfNeeded();
    if (recreateInitial.isErr()) {
      FAIL("recreateIfNeeded() before minimize failed: " << presentationErrorToString(recreateInitial.error()));
    }
    const SwapchainMetadata metadataBeforeMinimize = presentation->metadata();
    REQUIRE(metadataBeforeMinimize.imageCount > 0);

    minimizeWindowAndObserveZeroExtent(hwnd);
    presentation->notifyResized(Extent2D{0, 0});
    auto recreateMinimized = presentation->recreateIfNeeded();
    if (recreateMinimized.isErr()) {
      FAIL("recreateIfNeeded() while minimized failed: " << presentationErrorToString(recreateMinimized.error()));
    }
    // ADR-0016: a swapchain created at a prior non-zero extent is left
    // untouched when extent later becomes zero -- metadata must still
    // reflect the last successfully (re)created swapchain, not reset to
    // the no-swapchain default.
    requireMetadataUnchanged(metadataBeforeMinimize, presentation->metadata());

    // Repeated while still minimized: still Ok, metadata still retained.
    auto recreateMinimized2 = presentation->recreateIfNeeded();
    if (recreateMinimized2.isErr()) {
      FAIL("second recreateIfNeeded() while minimized failed: "
           << presentationErrorToString(recreateMinimized2.error()));
    }
    requireMetadataUnchanged(metadataBeforeMinimize, presentation->metadata());

    const Extent2D framebufferRestored = restoreWindowAndReadFramebufferExtent(hwnd);
    presentation->notifyResized(framebufferRestored);
    auto recreateRestored = presentation->recreateIfNeeded();
    if (recreateRestored.isErr()) {
      FAIL("recreateIfNeeded() after restore failed: " << presentationErrorToString(recreateRestored.error()));
    }
    const SwapchainMetadata metadataRestored = presentation->metadata();
    REQUIRE(metadataRestored.imageCount > 0);
    REQUIRE(metadataRestored.format != Format::Unknown);
    REQUIRE_FALSE(metadataRestored.extent.isZero());

    // Destroyed at the end of this scope, after the minimize/restore
    // cycle.
  }

  // Device destroyed explicitly, before Platform shutdown -- Presentation
  // must outlive nothing here (all four already destroyed above), and
  // Device must outlive every Presentation built from it (already true:
  // every presentation above was destroyed while device was still alive).
  device.reset();

  atlantis::platform::shutdown();
  platformGuard.markShutDown();

  // Drains whatever final events shutdown() produced (SurfaceDestroyed,
  // Quit, per Platform's own already-tested contract) so the process ends
  // cleanly; this file does not re-assert Platform's own event-ordering
  // contract, already covered by windows_platform_smoke_tests.cpp.
  static_cast<void>(atlantis::platform::processEvents());
}

#endif  // defined(_WIN32)

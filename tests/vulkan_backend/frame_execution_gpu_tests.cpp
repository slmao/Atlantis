#include <atlantis/platform/platform.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <catch2/catch_test_macros.hpp>

// GPU-required, windowed integration coverage for the full frame
// execution cycle (Spec 0006 / ADR-0019-0021), per
// plans/0006-rhi-render-graph-frame-execution-foundation.md Section 13 /
// Implementation Order Step 8. Windows-only, real Vulkan device, real
// window -- mirrors vulkan_presentation_gpu_tests.cpp's own structure and
// PlatformLifecycleGuard pattern. Drives the whole path exclusively
// through Atlantis's public API (RHI + RenderGraph) -- no Vulkan header,
// no Vk* type, and no direct Vulkan call appears anywhere in this file.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <memory>
#include <span>
#include <variant>

namespace {

using atlantis::render_graph::execute;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::rhi::ClearColorValue;
using atlantis::rhi::CommandList;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Presentation;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::ResourceState;
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::createPresentation;
using atlantis::vulkan_backend::DeviceCreateParams;

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

[[nodiscard]] const atlantis::platform::WindowResize* findNonZeroFramebufferResize(
    std::span<const atlantis::platform::PlatformEvent> events) {
  for (const auto& event : events) {
    if (const auto* resize = std::get_if<atlantis::platform::WindowResize>(&event)) {
      if (!resize->framebuffer.isZero()) return resize;
    }
  }
  return nullptr;
}

[[nodiscard]] bool containsZeroFramebufferResize(std::span<const atlantis::platform::PlatformEvent> events) {
  for (const auto& event : events) {
    if (const auto* resize = std::get_if<atlantis::platform::WindowResize>(&event)) {
      if (resize->framebuffer.isZero()) return true;
    }
  }
  return false;
}

constexpr int kMaxEventDrainIterations = 8;

[[nodiscard]] Extent2D resizeWindowAndReadFramebufferExtent(HWND hwnd, int width, int height) {
  SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
  for (int i = 0; i < kMaxEventDrainIterations; ++i) {
    const auto events = atlantis::platform::processEvents();
    if (const auto* resize = findNonZeroFramebufferResize(events)) {
      return Extent2D{resize->framebuffer.width, resize->framebuffer.height};
    }
  }
  FAIL("Timed out waiting for a non-zero WindowResize event after SetWindowPos(" << width << "x" << height << ")");
  return Extent2D{};
}

void minimizeWindowAndObserveZeroExtent(HWND hwnd) {
  ShowWindow(hwnd, SW_MINIMIZE);
  for (int i = 0; i < kMaxEventDrainIterations; ++i) {
    if (containsZeroFramebufferResize(atlantis::platform::processEvents())) return;
  }
  FAIL("Timed out waiting for a zero-extent WindowResize event after ShowWindow(SW_MINIMIZE)");
}

[[nodiscard]] Extent2D restoreWindowAndReadFramebufferExtent(HWND hwnd) {
  ShowWindow(hwnd, SW_RESTORE);
  for (int i = 0; i < kMaxEventDrainIterations; ++i) {
    const auto events = atlantis::platform::processEvents();
    if (const auto* resize = findNonZeroFramebufferResize(events)) {
      return Extent2D{resize->framebuffer.width, resize->framebuffer.height};
    }
  }
  FAIL("Timed out waiting for a non-zero WindowResize event after ShowWindow(SW_RESTORE)");
  return Extent2D{};
}

// Builds a single-pass RenderGraph that clears whatever RenderTarget it
// is bound to, compiles it, and drives it through render_graph::execute()
// into commandList -- the minimal "at least one GPU pass" acceptance
// scenario (Spec 0006). Returns nothing; failures surface via REQUIRE.
void recordOneClearPass(RenderTarget& target, CommandList& commandList) {
  RenderGraphBuilder builder;
  const auto resource = builder.declareResource("frame-target");
  const auto clearPass = builder.declarePass("clear");
  builder.writes(clearPass, resource, ResourceState::ColorAttachmentWrite);
  builder.setExecute(clearPass, [&target](CommandList& cmd) {
    cmd.clearColor(target, ClearColorValue{0.1f, 0.2f, 0.3f, 1.0f});
  });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  const std::vector<ResourceBinding> bindings{{compiled.value().resourceAt(0), &target}};
  execute(compiled.value(), bindings, commandList);
}

// Acquires, records one clear pass, submits, and presents exactly one
// frame. Returns true if a frame was actually drawn (Ok(non-null)
// acquire); false for a legitimate "nothing to draw this frame"
// (Ok(nullptr), e.g. zero extent) -- REQUIREs on any Err.
bool drawOneFrame(Presentation& presentation, Device& device) {
  auto acquireResult = presentation.acquireNextTarget();
  if (acquireResult.isErr()) {
    FAIL("acquireNextTarget() failed");
  }
  if (acquireResult.value() == nullptr) {
    return false;
  }
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  auto commandListResult = device.createCommandList();
  if (commandListResult.isErr()) {
    FAIL("createCommandList() failed");
  }
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  recordOneClearPass(*target, *commandList);

  auto submitResult = device.submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    FAIL("submit() failed");
  }

  auto presentResult = presentation.present(std::move(target), std::move(submitResult.value()));
  if (presentResult.isErr()) {
    FAIL("present() failed");
  }
  return true;
}

}  // namespace

TEST_CASE(
    "Frame execution: acquire, execute, submit, present, resize, minimize/restore, and cleanup",
    "[vulkan_backend][render_graph][frame_execution][gpu][integration]") {
  using atlantis::platform::PlatformEvent;
  using atlantis::platform::SurfaceCreated;

  const auto initResult = atlantis::platform::initialize();
  if (initResult.isErr()) {
    FAIL("platform::initialize() failed");
  }
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
  // process (validation.cpp), which CTest reports as a failed test.
  auto deviceResult =
      createDevice(DeviceCreateParams{.applicationName = "Atlantis Frame Execution GPU Tests", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    FAIL("createDevice() failed");
  }
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  SECTION("Acquire, execute, submit, present at least one real frame") {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed");
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    const Extent2D framebuffer = resizeWindowAndReadFramebufferExtent(hwnd, 400, 300);
    presentation->notifyResized(framebuffer);

    bool drewAtLeastOneFrame = false;
    for (int i = 0; i < 3; ++i) {
      if (drawOneFrame(*presentation, *device)) drewAtLeastOneFrame = true;
    }
    REQUIRE(drewAtLeastOneFrame);

    const auto idleResult = device->waitIdle();
    REQUIRE(idleResult.isOk());
  }

  SECTION("Resize mid-stream: frames continue after the swapchain recreates at a new extent") {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed");
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    presentation->notifyResized(resizeWindowAndReadFramebufferExtent(hwnd, 400, 300));
    REQUIRE(drawOneFrame(*presentation, *device));

    presentation->notifyResized(resizeWindowAndReadFramebufferExtent(hwnd, 500, 350));
    REQUIRE(drawOneFrame(*presentation, *device));

    REQUIRE(device->waitIdle().isOk());
  }

  SECTION("Zero extent (minimize): acquire returns nothing to draw, no crash, resumes on restore") {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed");
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    presentation->notifyResized(resizeWindowAndReadFramebufferExtent(hwnd, 400, 300));
    REQUIRE(drawOneFrame(*presentation, *device));

    minimizeWindowAndObserveZeroExtent(hwnd);
    presentation->notifyResized(Extent2D{0, 0});
    // Zero extent: acquireNextTarget() must return Ok(nullptr) -- nothing
    // to draw, not an error, no Vulkan call made on this path.
    REQUIRE_FALSE(drawOneFrame(*presentation, *device));

    presentation->notifyResized(restoreWindowAndReadFramebufferExtent(hwnd));
    REQUIRE(drawOneFrame(*presentation, *device));

    REQUIRE(device->waitIdle().isOk());
  }

  SECTION("Mid-frame exit: acquire succeeds but submit/present are never called for that frame") {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed");
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    presentation->notifyResized(resizeWindowAndReadFramebufferExtent(hwnd, 400, 300));
    REQUIRE(drawOneFrame(*presentation, *device));

    {
      auto acquireResult = presentation->acquireNextTarget();
      REQUIRE(acquireResult.isOk());
      REQUIRE(acquireResult.value() != nullptr);
      // target goes out of scope here, unpresented -- exercising the
      // "acquire, then exit" cleanup path (Plan 0006 Section 11). No
      // submit()/present() call is made for this acquired target.
    }

    // Required before Presentation/Device destruction whenever a
    // RenderTarget was acquired but never presented (ADR-0019).
    REQUIRE(device->waitIdle().isOk());
  }

  SECTION("Submit-then-exit: submit() succeeds but present() is never called") {
    auto presentationResult = createPresentation(*device, windowHandle);
    if (presentationResult.isErr()) {
      FAIL("createPresentation() failed");
    }
    std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

    presentation->notifyResized(resizeWindowAndReadFramebufferExtent(hwnd, 400, 300));
    REQUIRE(drawOneFrame(*presentation, *device));

    {
      auto acquireResult = presentation->acquireNextTarget();
      REQUIRE(acquireResult.isOk());
      REQUIRE(acquireResult.value() != nullptr);
      std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

      auto commandListResult = device->createCommandList();
      REQUIRE(commandListResult.isOk());
      std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());
      recordOneClearPass(*target, *commandList);

      auto submitResult = device->submit(std::move(commandList), *target);
      REQUIRE(submitResult.isOk());
      // submitResult.value() (the SubmissionSignal) and target both go
      // out of scope here, unpresented -- the submit()-followed-directly-
      // by-exit path Human Review confirmed legal (Plan 0006 Section 9).
    }

    REQUIRE(device->waitIdle().isOk());
  }

  device.reset();
  atlantis::platform::shutdown();
  platformGuard.markShutDown();
  static_cast<void>(atlantis::platform::processEvents());
}

#endif  // defined(_WIN32)

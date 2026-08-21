#pragma once

#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/lifecycle_state.h>
#include <atlantis/runtime/platform_session.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace atlantis::runtime {

// See Plan 0013 Section D4/D6/D7/D8. The composition object: owns
// Platform, Device, Presentation, Mesh, camera Buffer, depth Texture,
// and Material for their whole lifetime, and drives the fixed
// initialization -> windowed frame loop -> shutdown lifecycle. No
// abstract service interface, no DI container, no service locator --
// initializeSteps() calls the real, concrete Platform/RHI/Vulkan
// Backend/Renderer/Shader System/Asset System functions directly, by
// name, exactly as every existing composition root already does. Move-
// only (implicit, from its unique_ptr/optional/Mesh/Material members).
// Not internally thread-safe; caller-thread-only (ADR-0004).
class RuntimeApplication {
 public:
  RuntimeApplication() = default;
  ~RuntimeApplication();

  RuntimeApplication(const RuntimeApplication&) = delete;
  RuntimeApplication& operator=(const RuntimeApplication&) = delete;
  RuntimeApplication(RuntimeApplication&&) noexcept = default;
  RuntimeApplication& operator=(RuntimeApplication&&) noexcept = default;

  // One frame iteration: processEvents(), then (if a Presentation exists
  // and nothing has failed or requested close) acquire/format-check/
  // extent-check/draw/submit/present. Callable only while
  // shouldContinue() -- an ATLANTIS_CHECK guards the precondition.
  void runFrame();

  [[nodiscard]] bool shouldContinue() const noexcept;

  // Idempotent: waitIdle() (only if hasEverRun()), then destroys every
  // GPU resource this object owns, in the fixed order (D8). Does NOT
  // touch platformSession_ -- that member's own destructor, guaranteed
  // to run only after every member below it, is the sole place
  // platform::shutdown() is ever called.
  RuntimeExitReason shutdown();

 private:
  friend atlantis::Result<RuntimeApplication, RuntimeInitError> createRuntimeApplication(const BootstrapConfig&);

  atlantis::Result<std::monostate, RuntimeInitError> initializeSteps(const BootstrapConfig& config);

  // Declared in EXACTLY this order because C++ destroys non-static data
  // members in the REVERSE of their declaration order. platformSession_
  // is declared FIRST specifically so it is destroyed LAST, structurally
  // guaranteeing the OS window outlives every GPU resource below it
  // (Plan 0013 Section D4a). The remaining order (Material, Texture,
  // Buffer, Mesh, Presentation, Device) IS Spec 0013's/ADR-0046's fixed
  // Shutdown sequence, obtained the same way -- for free, from ordinary
  // member destruction.
  PlatformSession platformSession_;
  std::unique_ptr<atlantis::rhi::Device> device_;
  std::unique_ptr<atlantis::rhi::Presentation> presentation_;  // lazy: constructed on first SurfaceCreated
  std::optional<atlantis::renderer::Mesh> mesh_;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer_;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture_;  // lazy: first frame's extent-change check
  std::optional<atlantis::renderer::Material> material_;  // lazy: first frame's format-change check

  atlantis::renderer::Renderer renderer_;  // stateless, default-constructed
  RuntimeLifecycleTracker lifecycle_;
  RuntimeExitReason lastExitReason_ = RuntimeExitReason::Success;
  bool closeRequested_ = false;
  std::optional<atlantis::rhi::Format> lastSeenFormat_;
  std::optional<atlantis::rhi::Extent2D> lastSeenExtent_;
  atlantis::rhi::VertexInputLayout vertexInputLayout_;  // resolved once at init, reused for every Material rebuild
  std::vector<std::uint32_t> vertexSpirv_;               // retained for every Material rebuild
  std::vector<std::uint32_t> fragmentSpirv_;
};

[[nodiscard]] atlantis::Result<RuntimeApplication, RuntimeInitError> createRuntimeApplication(
    const BootstrapConfig& config);

}  // namespace atlantis::runtime

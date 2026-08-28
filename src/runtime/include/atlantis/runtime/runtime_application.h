#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/lifecycle_state.h>
#include <atlantis/runtime/platform_session.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/world.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace atlantis::runtime {

// See Plan 0013 Section D4/D6/D7/D8. The composition object: owns
// Platform, Device, Presentation, Mesh, camera Buffer, depth Texture,
// and Material for their whole lifetime, and drives the fixed
// initialization -> windowed frame loop -> shutdown lifecycle. No
// abstract service interface, no DI container, no service locator --
// initializeSteps() calls the real, concrete Platform/RHI/Vulkan
// Backend/Renderer/Shader System/Asset System functions directly, by
// name, exactly as every existing composition root already does.
// Move-constructible, NOT move-assignable (move-assignment is explicitly
// deleted, not merely left implicit -- see the operator='s own comment
// below). Not internally thread-safe; caller-thread-only (ADR-0004).
class RuntimeApplication {
 public:
  RuntimeApplication() = default;
  ~RuntimeApplication();

  RuntimeApplication(const RuntimeApplication&) = delete;
  RuntimeApplication& operator=(const RuntimeApplication&) = delete;
  RuntimeApplication(RuntimeApplication&&) noexcept = default;
  // Move-assignment is deleted (PR #63 review round), not defaulted: a
  // member-wise move-assignment runs in member DECLARATION order, so
  // platformSession_ (declared first) would be assigned -- and its own
  // shutdown-on-active-target logic run -- before device_/presentation_/
  // the GPU resources below it are torn down, closing the window while
  // they are still live. See platform_session.h's own comment.
  RuntimeApplication& operator=(RuntimeApplication&&) = delete;

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
  // Plan 0014 Section D-Step 6: the one narrowly-scoped friend needed for
  // the GPU smoke test's own V17 assertion (exactly 5 DrawItems reach
  // Renderer::drawFrame()) -- no other public/private API surface is
  // added for this; the capability (read how many renderable entities
  // the fixed validation scene currently has) has no design content of
  // its own, matching the same class of test-only accessor Plan 0014's
  // own V4 (World's generation-retirement test) already establishes.
  friend struct RuntimeSmokeTestAccess;

  // Move-constructs platformSession_ directly from an already-active
  // session (createRuntimeApplication()'s own Step 1). Deliberately not
  // wired in via assignment after default construction -- PlatformSession's
  // move-assignment operator is deleted (see platform_session.h), so this
  // constructor is the only way an active session ever enters a
  // RuntimeApplication.
  explicit RuntimeApplication(PlatformSession&& session) noexcept;

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
  // Plan 0015 Section D10: replaces the old std::optional<Mesh> mesh_ in
  // this exact declaration slot -- keyed by the AssetId every Renderable
  // references, populated only once, atomically, at the end of a
  // successful initializeSteps() (D10 step (g)). Occupying mesh_'s own
  // former slot preserves the existing, already-documented reverse-
  // destruction-order guarantee below without any new ordering rule:
  // every Mesh value the map owns still destructs after
  // Material/Texture/Buffer and before Presentation/Device.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap_;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer_;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture_;  // lazy: first frame's extent-change check

  // Plan 0018 Section P10 (Human Review Approval item 1): replaces the
  // former std::optional<Material> material_ in this exact slot. Every
  // GPU-realized resource is a std::unique_ptr<T> map VALUE, never a
  // value-typed entry -- a borrowed const T* into any of them has an
  // address fixed at first allocation, stable across map insertion,
  // rehash, or a whole-map move-assignment (the format-rebuild's own
  // atomic swap, P13). Two ownership layers, keyed differently on
  // purpose:
  //
  // Layer 1 -- format-independent, created exactly once, NEVER rebuilt
  // or moved by a format change (Spec 0018 D9 item 1). Keyed by TEXTURE
  // AssetId: two materials naming the same texture share one entry
  // (D10 dedup) -- SampledTextureCreateParams/SamplerCreateParams name
  // no colorFormat field, so nothing here is ever format-dependent.
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>
      sampledTextureResourceMap_;
  // Layer 1b -- also format-independent, created once per MATERIAL (not
  // shared across materials even when they share a texture, since
  // filter/addressMode are the Material DTO's own fields, Spec D2 -- no
  // per-value sampler-caching is attempted, matching Non-Goals' own "no
  // per-pipeline or per-material GPU-object caching/reuse across
  // distinct AssetIds," extended here to Sampler for the same reason).
  // Keyed by MATERIAL AssetId.
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::Sampler>> samplerResourceMap_;
  // Layer 2 -- format-DEPENDENT (Pipeline bakes in colorFormat), rebuilt
  // in full on every format change (D9). Keyed by MATERIAL AssetId.
  // Borrows (raw, non-owning pointers, Material's own existing
  // contract) into Layer 1/1b's already-stable addresses -- never into
  // a value-typed map slot.
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>
      materialResourceMap_;
  std::unique_ptr<atlantis::renderer::Material> fallbackMaterial_;  // lazy: first frame's format-change check; also
                                                                     // format-dependent, also rebuilt by D9

  // CPU-only, populated by Phase 1 (initializeSteps()), consumed/cleared
  // by Phase 2 (runFrame()) as each entry is realized -- no GPU handle,
  // value-typed is fine (nothing ever borrows a raw pointer into
  // these).
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap_;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap_;

  atlantis::renderer::Renderer renderer_;  // stateless, default-constructed
  // Plan 0014 Section D8: World owns no GPU resource and has no ordering
  // relationship to Device/Presentation/Mesh -- placed here, outside the
  // fixed reverse-destruction-order GPU-resource block above, not
  // because its own position is unconstrained in general, just because
  // no ordering constraint applies to it. Plan 0015 Section D2/D10:
  // retyped to std::optional -- World is move-constructible but NOT
  // move-assignable (ADR-0049/Spec 0014, unchanged), so publishing a
  // freshly-instantiated World requires in-place move-construction
  // (world_.emplace(std::move(world)), D10 step (g)), which a bare
  // World member cannot support. Empty (std::nullopt) until
  // initializeSteps() successfully reaches step (g); never reset by
  // shutdown() (it owns no GPU resource, matching today's behavior).
  std::optional<atlantis::world::World> world_;
  std::optional<atlantis::world::EntityId> activeCameraEntity_;  // cached for logging only; World itself is the source of truth
  RuntimeLifecycleTracker lifecycle_;
  RuntimeExitReason lastExitReason_ = RuntimeExitReason::Success;
  bool closeRequested_ = false;
  std::optional<atlantis::rhi::Format> lastSeenFormat_;
  std::optional<atlantis::rhi::Extent2D> lastSeenExtent_;
  atlantis::rhi::VertexInputLayout vertexInputLayout_;  // resolved once at init, reused for every Material rebuild
  std::vector<std::uint32_t> vertexSpirv_;               // retained for every Material rebuild
  std::vector<std::uint32_t> fragmentSpirv_;
  // Plan 0018 Section P10/P12: the second, MaterialKind::UnlitTextured
  // built-in shader pair's own resolved layout/SPIR-V -- mirrors
  // vertexInputLayout_/vertexSpirv_/fragmentSpirv_'s own role exactly,
  // resolved once at init (initializeSteps()), reused for every
  // realization (P12) and rebuild (P13) of a textured Material.
  atlantis::rhi::VertexInputLayout unlitTexturedVertexInputLayout_;
  std::vector<std::uint32_t> unlitTexturedVertexSpirv_;
  std::vector<std::uint32_t> unlitTexturedFragmentSpirv_;
};

[[nodiscard]] atlantis::Result<RuntimeApplication, RuntimeInitError> createRuntimeApplication(
    const BootstrapConfig& config);

}  // namespace atlantis::runtime

#include <atlantis/runtime/runtime_application.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/log.h>
#include <atlantis/platform/platform.h>
#include <atlantis/platform/platform_event.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/runtime/error_classification.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>
#include <atlantis/world/camera.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/vec3.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

// Plan 0013 Section D5: this file's own helpers (Vertex schema,
// loadSpirvFile(), the camera math) are duplicated -- not shared --
// from examples/minimal_renderer_demo/main.cpp and
// tests/image_regression/fixture/minimal_cube_fixture.cpp, matching
// this repository's own established "duplicated, not shared" precedent
// for this exact helper set (see minimal_cube_fixture.cpp's own
// top-of-file comment).

namespace atlantis::runtime {

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

struct Vertex {
  float position[3];
  float color[3];
};

[[nodiscard]] std::optional<std::vector<std::uint32_t>> loadSpirvFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;

  const std::streamsize sizeBytes = file.tellg();
  if (sizeBytes <= 0 || sizeBytes % 4 != 0) return std::nullopt;
  file.seekg(0);

  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  if (!file.read(reinterpret_cast<char*>(words.data()), sizeBytes)) return std::nullopt;
  return words;
}

[[nodiscard]] std::optional<VertexInputLayout> minimalMeshVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0014 Section D6: a second, small read of the same metadata file
// loadStaticMeshAsset() already consumes, to obtain the real, cooked
// AssetId -- matching this file's own already-established
// plain-std::ifstream pattern (loadSpirvFile() above), not a new I/O
// mechanism.
[[nodiscard]] std::optional<std::string> readFileToString(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return std::nullopt;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Plan 0014 Section D9: the fixed, six-entity validation scene, built
// once during initializeSteps() and never touched again by runFrame().
// Every World call here uses a handle this same function just created,
// so a Result::Err is only plausible from a genuine implementation bug
// (D8) -- still checked and propagated uniformly, not asserted, matching
// initializeSteps()'s own existing failure-handling style throughout.
[[nodiscard]] atlantis::Result<std::monostate, RuntimeInitError> buildValidationScene(
    atlantis::world::World& world, atlantis::asset_system::AssetId meshAssetId,
    std::optional<atlantis::world::EntityId>& activeCameraEntityOut) {
  using atlantis::world::Camera;
  using atlantis::world::EntityId;
  using atlantis::world::Renderable;
  using atlantis::world::Transform;
  using atlantis::world::Vec3;

  const auto fail = []() {
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::SceneConstructionFailed);
  };

  const auto makeCubeEntity = [&](Vec3 position, Vec3 eulerRadians) -> std::optional<EntityId> {
    const EntityId id = world.createEntity();
    Transform transform;
    transform.localPosition = position;
    transform.localEulerAnglesRadians = eulerRadians;
    if (world.setLocalTransform(id, transform).isErr()) return std::nullopt;
    if (world.setRenderable(id, Renderable{.meshAsset = meshAssetId}).isErr()) return std::nullopt;
    return id;
  };

  const std::optional<EntityId> a = makeCubeEntity(Vec3{-2.5f, 0.0f, 0.0f}, Vec3{});
  const std::optional<EntityId> b = makeCubeEntity(Vec3{-1.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.5236f, 0.0f});
  const std::optional<EntityId> c = makeCubeEntity(Vec3{1.0f, -0.5f, 0.0f}, Vec3{});
  const std::optional<EntityId> d = makeCubeEntity(Vec3{0.0f, 1.3f, 0.0f}, Vec3{0.0f, 0.7854f, 0.0f});
  const std::optional<EntityId> e = makeCubeEntity(Vec3{2.5f, 0.0f, 0.0f}, Vec3{0.2618f, 0.3491f, 0.0f});
  if (!a.has_value() || !b.has_value() || !c.has_value() || !d.has_value() || !e.has_value()) return fail();

  if (world.setParent(*d, *c).isErr()) return fail();

  const EntityId cameraEntity = world.createEntity();
  Transform cameraTransform;
  cameraTransform.localPosition = Vec3{0.0f, 2.2f, 7.0f};
  cameraTransform.localEulerAnglesRadians = Vec3{-0.3054f, 0.0f, 0.0f};
  if (world.setLocalTransform(cameraEntity, cameraTransform).isErr()) return fail();

  constexpr float kPi = 3.14159265f;
  const Camera camera{.fovYRadians = 60.0f * kPi / 180.0f, .nearZ = 0.1f, .farZ = 100.0f};
  if (world.setCamera(cameraEntity, camera).isErr()) return fail();
  if (world.setActiveCamera(cameraEntity).isErr()) return fail();

  activeCameraEntityOut = cameraEntity;
  return atlantis::Result<std::monostate, RuntimeInitError>::Ok({});
}

}  // namespace

RuntimeApplication::RuntimeApplication(PlatformSession&& session) noexcept
    : platformSession_(std::move(session)) {}

RuntimeApplication::~RuntimeApplication() { shutdown(); }

atlantis::Result<std::monostate, RuntimeInitError> RuntimeApplication::initializeSteps(
    const BootstrapConfig& config) {
  // Step 1 (Platform session) already ran in createRuntimeApplication(),
  // before this object existed -- platformSession_ was move-constructed
  // from it directly, never assigned into after the fact (PlatformSession's
  // move-assignment operator is deleted; see platform_session.h).

  // Step 2: shader load (SPIR-V + reflection JSON) and VertexInputLayout resolution.
  auto vertexSpirvOpt = loadSpirvFile(config.vertexShaderSpirvPath);
  auto fragmentSpirvOpt = loadSpirvFile(config.fragmentShaderSpirvPath);
  if (!vertexSpirvOpt.has_value() || !fragmentSpirvOpt.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shader SPIR-V from {} / {}", config.vertexShaderSpirvPath,
                        config.fragmentShaderSpirvPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto vertexReflectionResult = loadReflectionMetadata(config.vertexShaderReflectionPath);
  if (vertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadReflectionMetadata() failed for {}", config.vertexShaderReflectionPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto layoutOpt = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!layoutOpt.has_value()) {
    ATLANTIS_LOG_ERROR("minimalMeshVertexLayout(): reflected vertex-input attributes do not match the Vertex schema");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  vertexSpirv_ = std::move(vertexSpirvOpt.value());
  fragmentSpirv_ = std::move(fragmentSpirvOpt.value());
  vertexInputLayout_ = std::move(layoutOpt.value());

  // Step 3: Device.
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = config.applicationName, .enableValidationLayers = config.enableValidationLayers});
  if (deviceResult.isErr()) {
    ATLANTIS_LOG_ERROR("createDevice() failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::DeviceCreateFailed);
  }
  device_ = std::move(deviceResult.value());

  // Step 4: Asset load (minimal_cube runtime artifact -> StaticMeshAssetData).
  auto assetResult = atlantis::asset_system::loadStaticMeshAsset(config.assetArtifactPath, config.assetMetadataPath);
  if (assetResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadStaticMeshAsset() failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::AssetLoadFailed);
  }
  const atlantis::asset_system::StaticMeshAssetData& assetData = assetResult.value();

  // Plan 0014 Section D6: a second, small read of the same metadata file
  // just above, to obtain minimal_cube's real, cooked AssetId --
  // parseAssetMetadata() and AssetMetadata::assetId are both already
  // Accepted, public Asset System API; no change to Asset System's own
  // header, .cpp, or CMakeLists.txt.
  auto metadataTextOpt = readFileToString(config.assetMetadataPath);
  if (!metadataTextOpt.has_value()) {
    ATLANTIS_LOG_ERROR("failed to read asset metadata file for AssetId: {}", config.assetMetadataPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::AssetMetadataParseFailed);
  }
  auto metadataResult = atlantis::asset_system::parseAssetMetadata(*metadataTextOpt);
  if (metadataResult.isErr()) {
    ATLANTIS_LOG_ERROR("parseAssetMetadata() failed for {}", config.assetMetadataPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::AssetMetadataParseFailed);
  }
  knownMinimalCubeAssetId_ = metadataResult.value().assetId;

  // Step 5: Mesh, from the loaded asset's CPU-side bytes -- no intermediate copy.
  auto meshResult = createMesh(*device_, vertexInputLayout_, assetData.vertexBytes().data(),
                                assetData.vertexBytes().size(), assetData.indices().data(),
                                static_cast<std::uint32_t>(assetData.indices().size()));
  if (meshResult.isErr()) {
    ATLANTIS_LOG_ERROR("createMesh() failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::MeshCreateFailed);
  }
  mesh_ = std::move(meshResult.value());

  // Step 6: camera uniform Buffer.
  auto cameraBufferResult =
      device_->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (camera uniform) failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::CameraBufferCreateFailed);
  }
  cameraBuffer_ = std::move(cameraBufferResult.value());

  // No Material construction step here -- Plan 0013 Section D6/Spec
  // 0013's own Bootstrap Sequencing Detail: no real swapchain format is
  // known before the first SurfaceCreated, so Material's first
  // construction happens inside runFrame()'s own format-change check
  // below, exactly the code path that later handles every subsequent
  // format change identically.

  // Plan 0014 Section D9: the fixed, six-entity validation scene --
  // built once here, never mutated by runFrame() (updateTransforms()
  // still runs every frame, recomputing the same, unchanged world
  // matrices each time).
  auto sceneResult = buildValidationScene(world_, knownMinimalCubeAssetId_, activeCameraEntity_);
  if (sceneResult.isErr()) {
    ATLANTIS_LOG_ERROR("buildValidationScene() failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(sceneResult.error());
  }

  lifecycle_.markRunning();
  return atlantis::Result<std::monostate, RuntimeInitError>::Ok(std::monostate{});
}

atlantis::Result<RuntimeApplication, RuntimeInitError> createRuntimeApplication(const BootstrapConfig& config) {
  // Step 1: Platform session -- created before RuntimeApplication exists,
  // so platformSession_ can be move-CONSTRUCTED directly into it below.
  // On Err, no RuntimeApplication is ever constructed; there is nothing
  // to tear down.
  auto sessionResult = createPlatformSession();
  if (sessionResult.isErr()) {
    ATLANTIS_LOG_ERROR("createPlatformSession() failed");
    return atlantis::Result<RuntimeApplication, RuntimeInitError>::Err(RuntimeInitError::PlatformInitFailed);
  }

  RuntimeApplication app(std::move(sessionResult.value()));
  app.lifecycle_.beginInitializing();
  auto result = app.initializeSteps(config);
  if (result.isErr()) {
    return atlantis::Result<RuntimeApplication, RuntimeInitError>::Err(result.error());
  }
  return atlantis::Result<RuntimeApplication, RuntimeInitError>::Ok(std::move(app));
}

bool RuntimeApplication::shouldContinue() const noexcept {
  return !platform::shouldQuit() && !closeRequested_ && lifecycle_.state() != RuntimeLifecycleState::Failed;
}

void RuntimeApplication::runFrame() {
  ATLANTIS_CHECK_MSG(shouldContinue(), "runFrame() called while !shouldContinue()");

  for (const auto& event : platform::processEvents()) {
    if (const auto* created = std::get_if<platform::SurfaceCreated>(&event)) {
      if (presentation_) {
        ATLANTIS_LOG_ERROR("SurfaceCreated observed while a Presentation already exists");
        lifecycle_.markFailed();
        continue;
      }
      auto presentationResult = atlantis::vulkan_backend::createPresentation(*device_, created->handle);
      if (presentationResult.isErr()) {
        ATLANTIS_LOG_ERROR("createPresentation() failed");
        lifecycle_.markFailed();
        continue;
      }
      presentation_ = std::move(presentationResult.value());
      ATLANTIS_LOG_INFO("Presentation created");
    } else if (const auto* resize = std::get_if<platform::WindowResize>(&event)) {
      if (presentation_) {
        const Extent2D framebufferExtent{resize->framebuffer.width, resize->framebuffer.height};
        presentation_->notifyResized(framebufferExtent);
      }
    } else if (std::holds_alternative<platform::WindowCloseRequested>(event)) {
      closeRequested_ = true;
    } else if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
      if (presentation_) {
        ATLANTIS_LOG_ERROR("SurfaceDestroyed observed while a Presentation still exists");
        lifecycle_.markFailed();
      }
    }
    // Quit / FocusGained / FocusLost / ApplicationPause / ApplicationResume: no state change.
  }

  if (!presentation_ || closeRequested_ || lifecycle_.state() == RuntimeLifecycleState::Failed) {
    return;
  }

  auto acquireResult = presentation_->acquireNextTarget();
  if (acquireResult.isErr()) {
    ATLANTIS_LOG_ERROR("acquireNextTarget() failed");
    static_cast<void>(classifyPresentationError(acquireResult.error()));  // classification result: always Unrecoverable
    lifecycle_.markFailed();
    return;
  }
  if (acquireResult.value() == nullptr) {
    return;  // zero extent, or an internally-deferred out-of-date swapchain
  }
  std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());

  // Format-change check: create-before-destroy Material rebuild.
  const atlantis::rhi::Format currentFormat = presentation_->metadata().format;
  if (!lastSeenFormat_.has_value() || currentFormat != *lastSeenFormat_) {
    auto newMaterialResult = createMaterial(
        *device_, {.vertexShader = {.spirvWords = vertexSpirv_.data(), .wordCount = vertexSpirv_.size()},
                   .fragmentShader = {.spirvWords = fragmentSpirv_.data(), .wordCount = fragmentSpirv_.size()},
                   .vertexInputLayout = vertexInputLayout_,
                   .colorFormat = currentFormat,
                   .depthFormat = DepthFormat::D32Sfloat,
                   .pushConstantSizeBytes = sizeof(float) * 16});
    if (newMaterialResult.isErr()) {
      ATLANTIS_LOG_ERROR(
          "createMaterial() failed during format-change rebuild -- keeping the existing Material and retrying next "
          "frame");
      // lastSeenFormat_ intentionally NOT updated -- retry next frame.
    } else {
      material_ = std::move(newMaterialResult.value());  // old Pipeline (if any) destroyed HERE, only after the new
                                                           // one already succeeded.
      lastSeenFormat_ = currentFormat;
    }
  }

  // Extent-change check: recreate the depth Texture only -- Pipeline is untouched (dynamic viewport/scissor).
  const Extent2D currentExtent = target->extent();
  if (!lastSeenExtent_.has_value() || !(currentExtent == *lastSeenExtent_)) {
    auto newTextureResult = device_->createTexture({.extent = currentExtent, .format = DepthFormat::D32Sfloat});
    if (newTextureResult.isErr()) {
      ATLANTIS_LOG_ERROR(
          "createTexture() (depth) failed during resize -- keeping the existing depth Texture and retrying next "
          "frame");
      // lastSeenExtent_ intentionally NOT updated -- retry next frame.
    } else {
      depthTexture_ = std::move(newTextureResult.value());
      lastSeenExtent_ = currentExtent;
    }
  }

  if (!material_ || !depthTexture_) {
    return;  // nothing valid to draw yet -- target dropped via RAII, no leaked GPU state
  }

  // Plan 0014 Section D8: World-driven extraction replaces the prior
  // fixed single-cube camera write + DrawItem build.
  world_.updateTransforms();

  const std::optional<atlantis::world::EntityId> activeCamera = world_.activeCamera();
  if (!activeCamera.has_value()) {
    // This Plan's own validation scene always sets an active camera at
    // construction (D9) -- reaching this path indicates a genuine
    // construction bug, the same "should never happen in correct
    // operation" category Spec 0013 already established for a second
    // SurfaceCreated/an unexpected SurfaceDestroyed.
    ATLANTIS_LOG_ERROR("runFrame(): World has no active camera");
    lifecycle_.markFailed();
    return;
  }
  activeCameraEntity_ = activeCamera;  // cached for logging only; World itself is the source of truth

  const auto cameraWorldMatrixResult = world_.getWorldMatrix(*activeCamera);
  ATLANTIS_CHECK_MSG(cameraWorldMatrixResult.isOk(),
                      "runFrame(): getWorldMatrix() failed for the handle activeCamera() just returned");
  const auto cameraComponentResult = world_.getCamera(*activeCamera);
  ATLANTIS_CHECK_MSG(cameraComponentResult.isOk(),
                      "runFrame(): getCamera() failed for the handle activeCamera() just returned");
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();

  const float aspect =
      currentExtent.height != 0 ? static_cast<float>(currentExtent.width) / static_cast<float>(currentExtent.height)
                                 : 1.0f;
  const auto extractionResult = extractCameraMatrices(cameraWorldMatrixResult.value(), cameraComponent.fovYRadians,
                                                        cameraComponent.nearZ, cameraComponent.farZ, aspect);
  if (extractionResult.isErr()) {
    // This Plan's own fixed camera Transform/Camera values (D9) are
    // chosen to never be degenerate -- reaching this path is likewise an
    // unrecoverable construction-bug indicator, not a per-frame
    // condition to retry.
    ATLANTIS_LOG_ERROR("runFrame(): extractCameraMatrices() failed");
    lifecycle_.markFailed();
    return;
  }

  auto* cameraData = static_cast<float*>(cameraBuffer_->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = extractionResult.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = extractionResult.value().projection[i];

  std::vector<DrawItem> drawItems;
  for (const atlantis::world::EntityId& id : world_.renderableEntities()) {
    const auto renderableResult = world_.getRenderable(id);
    ATLANTIS_CHECK_MSG(renderableResult.isOk(),
                        "runFrame(): getRenderable() failed for a handle renderableEntities() just returned");
    const auto resolveResult = resolveMeshAsset(renderableResult.value().meshAsset, knownMinimalCubeAssetId_);
    if (resolveResult.isErr()) {
      // Recoverable, per-entity: a single bad reference should not halt
      // an otherwise-valid scene, matching the general "keep going, log,
      // retry/skip" philosophy the existing format-/extent-change retry
      // logic above already establishes.
      ATLANTIS_LOG_ERROR("runFrame(): resolveMeshAsset() could not resolve a Renderable entity's own AssetId");
      continue;
    }
    const auto worldMatrixResult = world_.getWorldMatrix(id);
    ATLANTIS_CHECK_MSG(worldMatrixResult.isOk(),
                        "runFrame(): getWorldMatrix() failed for a handle renderableEntities() just returned");

    DrawItem item;
    item.mesh = &*mesh_;
    item.material = &*material_;
    item.objectToWorld = worldMatrixResult.value();
    drawItems.push_back(item);
  }

  auto commandListResult = device_->createCommandList();
  if (commandListResult.isErr()) {
    ATLANTIS_LOG_ERROR("createCommandList() failed");
    lifecycle_.markFailed();
    return;
  }
  std::unique_ptr<atlantis::rhi::CommandList> commandList = std::move(commandListResult.value());

  renderer_.drawFrame(*commandList, *target, *depthTexture_, *cameraBuffer_, drawItems,
                       atlantis::rhi::ResourceState::PresentSource);

  auto submitResult = device_->submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    ATLANTIS_LOG_ERROR("submit() failed");
    static_cast<void>(classifySubmitError(submitResult.error()));
    lifecycle_.markFailed();
    return;
  }

  auto presentResult = presentation_->present(std::move(target), std::move(submitResult.value()));
  if (presentResult.isErr()) {
    ATLANTIS_LOG_ERROR("present() failed");
    static_cast<void>(classifyPresentationError(presentResult.error()));
    lifecycle_.markFailed();
    return;
  }
}

RuntimeExitReason RuntimeApplication::shutdown() {
  if (lifecycle_.state() == RuntimeLifecycleState::ShutDown) {
    return lastExitReason_;  // idempotent
  }
  bool hadFailure = (lifecycle_.state() == RuntimeLifecycleState::Failed);
  lifecycle_.beginShutdown();

  // waitIdle() is meaningful only if this object ever reached Running --
  // before that, runFrame() (the only place Device::submit() is ever
  // called) has never run, so there is nothing outstanding to wait for.
  if (device_ && lifecycle_.hasEverRun()) {
    auto result = device_->waitIdle();
    if (result.isErr()) {
      ATLANTIS_LOG_ERROR("waitIdle() failed during shutdown");
      hadFailure = true;  // a wait that cannot complete cleanly is itself an unrecoverable
                           // condition for this run; teardown proceeds regardless.
    }
  }

  material_.reset();
  depthTexture_.reset();
  cameraBuffer_.reset();
  mesh_.reset();
  presentation_.reset();
  device_.reset();
  // platformSession_ is deliberately NOT touched here -- its own
  // destructor (guaranteed to run only after every member above) is the
  // ONLY place platform::shutdown() is ever called.

  lifecycle_.markShutDown();
  lastExitReason_ =
      hadFailure ? RuntimeExitReason::UnrecoverableRuntimeError : RuntimeExitReason::Success;
  return lastExitReason_;
}

}  // namespace atlantis::runtime

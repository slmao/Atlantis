#include "world_scene_fixture.h"

#include "minimal_cube_fixture.h"

#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
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
#include <fstream>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

// Plan 0014 Section D10: duplicates src/runtime/'s own scene-construction
// and per-frame extraction logic independently -- not a shared
// "Extraction module" (ADR-0051's own Alternatives Considered), matching
// this codebase's own established "duplicated, not shared" precedent for
// exactly this kind of small camera-math/scene helper (a fourth copy of
// lookAt()/perspective()/identityMatrix(), after
// examples/minimal_renderer_demo, minimal_cube_fixture.cpp, and
// src/runtime/'s scene_extraction.cpp).

namespace atlantis::image_regression {

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;
using atlantis::world::Camera;
using atlantis::world::EntityId;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::Vec3;
using atlantis::world::World;

[[nodiscard]] std::optional<std::vector<std::uint32_t>> loadSpirvFile(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;

  const std::streamsize sizeBytes = file.tellg();
  if (sizeBytes <= 0 || sizeBytes % 4 != 0) return std::nullopt;
  file.seekg(0);

  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  if (!file.read(reinterpret_cast<char*>(words.data()), sizeBytes)) return std::nullopt;
  return words;
}

// Plan 0017 Section D5/ADR-0058: gains a trailing UV0 field to match
// the mesh artifact's own new 32-byte layout. Plan 0020 Section P1/P4/
// ADR-0063: gains a further trailing normal field, matching the
// artifact's own new 44-byte layout. minimal_mesh.slang declares
// neither a UV nor a normal input, so the schema below stays unchanged
// (position@0, color@1) -- these trailing bytes are present in every
// vertex this fixture uploads but are never read by this pipeline.
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
  float normal[3];
};
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(offsetof(Vertex, position) == atlantis::asset_system::kMeshArtifactPositionOffsetBytes);
static_assert(offsetof(Vertex, color) == atlantis::asset_system::kMeshArtifactColorOffsetBytes);
static_assert(offsetof(Vertex, uv) == atlantis::asset_system::kMeshArtifactUv0OffsetBytes);
static_assert(offsetof(Vertex, normal) == atlantis::asset_system::kMeshArtifactNormalOffsetBytes);
static_assert(sizeof(Vertex) == atlantis::asset_system::kMeshArtifactVertexStrideBytes);

[[nodiscard]] std::optional<VertexInputLayout> minimalMeshVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

using Mat4 = std::array<float, 16>;

[[nodiscard]] Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

[[nodiscard]] Mat4 lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ) {
  float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
  const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= fLen;
  fy /= fLen;
  fz /= fLen;

  const float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
  float sx = fy * upZ - fz * upY;
  float sy = fz * upX - fx * upZ;
  float sz = fx * upY - fy * upX;
  const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz);
  sx /= sLen;
  sy /= sLen;
  sz /= sLen;

  const float ux = sy * fz - sz * fy;
  const float uy = sz * fx - sx * fz;
  const float uz = sx * fy - sy * fx;

  Mat4 result = identityMatrix();
  result[0] = sx;
  result[4] = sy;
  result[8] = sz;
  result[1] = ux;
  result[5] = uy;
  result[9] = uz;
  result[2] = -fx;
  result[6] = -fy;
  result[10] = -fz;
  result[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
  result[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
  result[14] = fx * eyeX + fy * eyeY + fz * eyeZ;
  return result;
}

[[nodiscard]] Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

// Duplicates src/runtime/scene_extraction.cpp's own extractCameraMatrices()
// algorithm (eye+forward-only extraction, both degenerate checks), not
// called across any module boundary -- this fixture links neither
// Atlantis::RuntimeHost nor scene_extraction.cpp's own translation unit.
constexpr float kDegenerateLengthEpsilon = 1e-6f;

struct V3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};

[[nodiscard]] float length(const V3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
[[nodiscard]] V3 cross(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] std::optional<Mat4> extractView(const Mat4& cameraWorldMatrix) {
  const V3 negColumn2{-cameraWorldMatrix[8], -cameraWorldMatrix[9], -cameraWorldMatrix[10]};
  const float forwardLen = length(negColumn2);
  if (forwardLen < kDegenerateLengthEpsilon) return std::nullopt;
  const V3 forward{negColumn2.x / forwardLen, negColumn2.y / forwardLen, negColumn2.z / forwardLen};

  const V3 eye{cameraWorldMatrix[12], cameraWorldMatrix[13], cameraWorldMatrix[14]};

  const V3 worldUp{0.0f, 1.0f, 0.0f};
  if (length(cross(forward, worldUp)) < kDegenerateLengthEpsilon) return std::nullopt;

  return lookAt(eye.x, eye.y, eye.z, eye.x + forward.x, eye.y + forward.y, eye.z + forward.z);
}

// Plan 0014 Section D9: the fixed, six-entity validation scene -- built
// once here via the same World public API calls Runtime uses,
// duplicated construction code, not a shared "build the scene"
// function.
[[nodiscard]] bool buildValidationScene(World& world, atlantis::asset_system::AssetId meshAssetId) {
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
  if (!a.has_value() || !b.has_value() || !c.has_value() || !d.has_value() || !e.has_value()) return false;

  if (world.setParent(*d, *c).isErr()) return false;

  const EntityId cameraEntity = world.createEntity();
  Transform cameraTransform;
  cameraTransform.localPosition = Vec3{0.0f, 2.2f, 7.0f};
  cameraTransform.localEulerAnglesRadians = Vec3{-0.3054f, 0.0f, 0.0f};
  if (world.setLocalTransform(cameraEntity, cameraTransform).isErr()) return false;

  constexpr float kPi = 3.14159265f;
  const Camera camera{.fovYRadians = 60.0f * kPi / 180.0f, .nearZ = 0.1f, .farZ = 100.0f};
  if (world.setCamera(cameraEntity, camera).isErr()) return false;
  if (world.setActiveCamera(cameraEntity).isErr()) return false;

  return true;
}

}  // namespace

Result<WorldSceneFixture, WorldSceneFixtureSetupError> setUpWorldSceneFixture(const char* artifactPath,
                                                                                const char* metadataPath) {
  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ShaderLoadFailed);
  }

  auto vertexReflectionResult = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflectionResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ShaderLoadFailed);
  }
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ShaderLoadFailed);
  }

  auto assetResult = atlantis::asset_system::loadStaticMeshAsset(artifactPath, metadataPath);
  if (assetResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::AssetLoadFailed);
  }
  const atlantis::asset_system::StaticMeshAssetData& meshData = assetResult.value();

  std::ifstream metadataFile(metadataPath, std::ios::binary);
  std::ostringstream metadataBuffer;
  metadataBuffer << metadataFile.rdbuf();
  auto metadataResult = atlantis::asset_system::parseAssetMetadata(metadataBuffer.str());
  if (metadataResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::AssetLoadFailed);
  }

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (World Scene)", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::DeviceCreationFailed);
  }

  WorldSceneFixture fixture;
  fixture.device = std::move(deviceResult.value());
  fixture.knownMinimalCubeAssetId = metadataResult.value().assetId;

  auto meshResult = createMesh(*fixture.device, *vertexInputLayout, meshData.vertexBytes().data(),
                                meshData.vertexBytes().size(), meshData.indices().data(),
                                static_cast<std::uint32_t>(meshData.indices().size()));
  if (meshResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ResourceCreationFailed);
  }
  fixture.mesh = std::move(meshResult.value());

  auto cameraBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ResourceCreationFailed);
  }
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  auto materialResult = createMaterial(
      *fixture.device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                         .vertexInputLayout = *vertexInputLayout,
                         .colorFormat = kFixtureColorFormat,
                         .depthFormat = DepthFormat::D32Sfloat,
                         .pushConstantSizeBytes = sizeof(float) * 16});
  if (materialResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ResourceCreationFailed);
  }
  fixture.material = std::move(materialResult.value());

  const Extent2D extent{kFixtureExtentPixels, kFixtureExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ResourceCreationFailed);
  }
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = extent, .format = kFixtureColorFormat});
  if (offscreenTargetResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ResourceCreationFailed);
  }
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::ResourceCreationFailed);
  }
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  if (!buildValidationScene(fixture.world, fixture.knownMinimalCubeAssetId)) {
    return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Err(
        WorldSceneFixtureSetupError::SceneConstructionFailed);
  }

  return Result<WorldSceneFixture, WorldSceneFixtureSetupError>::Ok(std::move(fixture));
}

Result<PixelBuffer, WorldSceneFixtureRenderError> renderOneWorldSceneFrame(WorldSceneFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(WorldSceneFixtureRenderError::AcquireFailed);
  }
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  fixture.world.updateTransforms();

  const auto activeCamera = fixture.world.activeCamera();
  if (!activeCamera.has_value()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(WorldSceneFixtureRenderError::NoActiveCamera);
  }
  const auto cameraWorldMatrixResult = fixture.world.getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world.getCamera(*activeCamera);
  if (cameraWorldMatrixResult.isErr() || cameraComponentResult.isErr()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(WorldSceneFixtureRenderError::ExtractionFailed);
  }
  const auto view = extractView(cameraWorldMatrixResult.value());
  if (!view.has_value()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(WorldSceneFixtureRenderError::ExtractionFailed);
  }
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();
  const Mat4 projection = perspective(cameraComponent.fovYRadians, 1.0f, cameraComponent.nearZ, cameraComponent.farZ);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = (*view)[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];

  std::vector<DrawItem> drawItems;
  for (const auto& id : fixture.world.renderableEntities()) {
    const auto renderableResult = fixture.world.getRenderable(id);
    if (renderableResult.isErr()) continue;
    if (renderableResult.value().meshAsset != fixture.knownMinimalCubeAssetId) continue;
    const auto worldMatrixResult = fixture.world.getWorldMatrix(id);
    if (worldMatrixResult.isErr()) continue;

    DrawItem item;
    item.mesh = &*fixture.mesh;
    item.material = &*fixture.material;
    item.objectToWorld = worldMatrixResult.value();
    drawItems.push_back(item);
  }

  auto commandListResult = fixture.device->createCommandList();
  if (commandListResult.isErr()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(
        WorldSceneFixtureRenderError::CommandListCreationFailed);
  }
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                      rhi::ResourceState::TransferSource);

  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(
        WorldSceneFixtureRenderError::CommandListCreationFailed);
  }
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(WorldSceneFixtureRenderError::SubmitFailed);
  }

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) {
    return Result<PixelBuffer, WorldSceneFixtureRenderError>::Err(WorldSceneFixtureRenderError::WaitIdleFailed);
  }

  PixelBuffer result;
  result.width = kFixtureExtentPixels;
  result.height = kFixtureExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return Result<PixelBuffer, WorldSceneFixtureRenderError>::Ok(std::move(result));
}

}  // namespace atlantis::image_regression

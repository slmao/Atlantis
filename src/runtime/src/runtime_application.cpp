#include <atlantis/runtime/runtime_application.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/log.h>
#include <atlantis/platform/platform.h>
#include <atlantis/platform/platform_event.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/runtime/error_classification.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/runtime/scene_load.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>
#include <atlantis/world/camera.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <type_traits>
#include <unordered_map>
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
using atlantis::renderer::DrawItem;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Plan 0017 Section D5/ADR-0058: gains a trailing UV0 field to match
// the mesh artifact's own new 32-byte layout. Plan 0020 Section P1/P4/
// ADR-0063: gains a further trailing normal field, matching the
// artifact's own new 44-byte layout. minimal_mesh.slang declares
// neither a UV nor a normal input, so the schema below stays unchanged
// (position@0, color@1) -- these trailing bytes are present in every
// vertex this composition root uploads but are never read by this
// pipeline.
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

// Plan 0015 Section D10 step (g) / final review round (2026-08-24):
// the two-step publish in initializeSteps() below (world_.emplace(),
// then meshResourceMap_'s own move-assignment) is genuinely atomic in
// effect, not merely "unlikely to fail" -- both operations are
// unconditionally noexcept, locked down here as a compile-time-
// enforced invariant rather than argued in prose or guarded with a
// catch/rollback. World's own move constructor is noexcept by its own
// declaration (world.h); std::unordered_map<AssetId, Mesh>'s own
// move-assignment operator is noexcept per the standard's own
// [unord.map] clause whenever its Allocator/Hash/KeyEqual satisfy that
// clause's own noexcept condition, which the default
// std::allocator/std::hash<AssetId>/std::equal_to<AssetId> this map
// instantiates with all do. Given both hold, the first publish step
// cannot throw if it runs at all; and since world_ starts
// std::nullopt and is written exactly once over RuntimeApplication's
// own lifetime (initializeSteps() runs once, from createRuntimeApplication()),
// there is no prior engaged state for emplace() to destroy first
// either. There is therefore no reachable state where world_ is
// populated but meshResourceMap_ is not, or vice versa -- if either of
// these static_asserts were ever to start failing (e.g. a future
// change to Mesh's own type altering Hash/KeyEqual), that would be a
// compile error here, not a latent runtime risk discovered later.
static_assert(std::is_nothrow_move_constructible_v<atlantis::world::World>,
              "world_.emplace(std::move(world)) in initializeSteps() requires World's own move constructor to be "
              "noexcept for the scene-load publish step to be genuinely atomic");
static_assert(
    std::is_nothrow_move_assignable_v<decltype(std::declval<SceneLoadOutcome>().meshResourceMap)>,
    "meshResourceMap_ = std::move(outcome.meshResourceMap) in initializeSteps() requires this move-assignment to "
    "be noexcept for the scene-load publish step to be genuinely atomic");
// Plan 0018 Section P11: two more instantiations of the exact same
// argument -- std::unordered_map<AssetId, T>'s own move-assignment is
// noexcept under the default allocator/hash/equality regardless of T,
// so widening the publish from two moves to four needs no new argument,
// only these two additional static_asserts (Verification Checklist V23).
static_assert(
    std::is_nothrow_move_assignable_v<decltype(std::declval<SceneLoadOutcome>().materialDataMap)>,
    "materialDataMap_ = std::move(outcome.materialDataMap) in initializeSteps() requires this move-assignment to "
    "be noexcept for the scene-load publish step to be genuinely atomic");
static_assert(
    std::is_nothrow_move_assignable_v<decltype(std::declval<SceneLoadOutcome>().textureDataMap)>,
    "textureDataMap_ = std::move(outcome.textureDataMap) in initializeSteps() requires this move-assignment to "
    "be noexcept for the scene-load publish step to be genuinely atomic");

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

// Plan 0018 Section P10/P12: the MaterialKind::UnlitTextured shader
// pair's own vertex schema -- reads the SAME Vertex struct/mesh stride
// every mesh in this codebase's own asset pipeline already uses (the
// mesh artifact's own real, current 44-byte position+color+UV0+normal
// layout -- Plan 0020 Section P1/P4/ADR-0063, widened from Spec 0017's
// original 32-byte position+color+UV0 layout -- is a property of the
// MESH artifact, not of which material a given DrawItem happens to
// bind), mapping position@0 and uv@1 into textured_quad.slang's own two
// vertex-input locations -- color (bytes 12-23) and normal (bytes
// 32-43) are both deliberately left unread, mirroring
// textured_quad_fixture.cpp's own already-proven, identical schema
// exactly.
[[nodiscard]] std::optional<VertexInputLayout> unlitTexturedVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0019 Section P15: the MaterialKind::LitTextured shader pair's own
// vertex schema -- position@0, uv@1, and (unlike unlitTexturedVertexLayout()
// above) normal@2, cross-checked against lit_textured.slang's own
// VertexInput location by toVertexInputLayout()'s own existing
// reflection-matching (a location/offset typo fails at Implementation
// time with Result::Err(MappingError::LocationNotFoundInSchema), not
// silently at runtime).
[[nodiscard]] std::optional<VertexInputLayout> litTexturedVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0023 Milestone 4/5: the MaterialKind::PbrDirectLit shader pair's
// own vertex schema -- byte-identical to litTexturedVertexLayout()'s
// own schema above (position@0/uv@1/normal@2, matching
// pbr_direct_lit.slang's own VertexInput exactly), kept as its own,
// separately-named function rather than reusing litTexturedVertexLayout()
// directly -- matching this file's own established one-function-per-
// shader-pair convention, and cross-checked against pbr_direct_lit's
// own real reflection metadata, not lit_textured's.
[[nodiscard]] std::optional<VertexInputLayout> pbrDirectLitVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0024 Milestone 4/6: the output-transform pass's own vertex
// schema -- NOT sourced from the mesh Vertex struct above at all (this
// is the fixed, never-scene-content fullscreen triangle, Milestone 4),
// exactly one Float2 clip-space position at location 0, offset 0,
// stride 8 bytes. Both output-transform shader variants (unorm/srgb)
// declare an identical vertex stage (Milestone 4), so this one
// function is reused for both -- called twice, once per variant's own
// real reflection metadata, cross-validated independently each time.
[[nodiscard]] std::optional<VertexInputLayout> outputTransformVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(float) * 2);
  if (result.isErr()) return std::nullopt;
  return result.value();
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

  // Step 2b (Plan 0018 Section P10): the second, MaterialKind::UnlitTextured
  // built-in shader pair -- same shape as step 2 above, mirrored exactly.
  auto unlitTexturedVertexSpirvOpt = loadSpirvFile(config.unlitTexturedVertexShaderSpirvPath);
  auto unlitTexturedFragmentSpirvOpt = loadSpirvFile(config.unlitTexturedFragmentShaderSpirvPath);
  if (!unlitTexturedVertexSpirvOpt.has_value() || !unlitTexturedFragmentSpirvOpt.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shader SPIR-V from {} / {}", config.unlitTexturedVertexShaderSpirvPath,
                        config.unlitTexturedFragmentShaderSpirvPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto unlitTexturedVertexReflectionResult = loadReflectionMetadata(config.unlitTexturedVertexShaderReflectionPath);
  if (unlitTexturedVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadReflectionMetadata() failed for {}", config.unlitTexturedVertexShaderReflectionPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto unlitTexturedLayoutOpt = unlitTexturedVertexLayout(unlitTexturedVertexReflectionResult.value());
  if (!unlitTexturedLayoutOpt.has_value()) {
    ATLANTIS_LOG_ERROR(
        "unlitTexturedVertexLayout(): reflected vertex-input attributes do not match the Vertex schema");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  unlitTexturedVertexSpirv_ = std::move(unlitTexturedVertexSpirvOpt.value());
  unlitTexturedFragmentSpirv_ = std::move(unlitTexturedFragmentSpirvOpt.value());
  unlitTexturedVertexInputLayout_ = std::move(unlitTexturedLayoutOpt.value());

  // Step 2c (Plan 0019 Section P6/P11): the third, MaterialKind::LitTextured
  // built-in shader pair -- same shape as step 2b above, mirrored exactly.
  auto litTexturedVertexSpirvOpt = loadSpirvFile(config.litTexturedVertexShaderSpirvPath);
  auto litTexturedFragmentSpirvOpt = loadSpirvFile(config.litTexturedFragmentShaderSpirvPath);
  if (!litTexturedVertexSpirvOpt.has_value() || !litTexturedFragmentSpirvOpt.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shader SPIR-V from {} / {}", config.litTexturedVertexShaderSpirvPath,
                        config.litTexturedFragmentShaderSpirvPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto litTexturedVertexReflectionResult = loadReflectionMetadata(config.litTexturedVertexShaderReflectionPath);
  if (litTexturedVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadReflectionMetadata() failed for {}", config.litTexturedVertexShaderReflectionPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto litTexturedLayoutOpt = litTexturedVertexLayout(litTexturedVertexReflectionResult.value());
  if (!litTexturedLayoutOpt.has_value()) {
    ATLANTIS_LOG_ERROR("litTexturedVertexLayout(): reflected vertex-input attributes do not match the Vertex schema");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  litTexturedVertexSpirv_ = std::move(litTexturedVertexSpirvOpt.value());
  litTexturedFragmentSpirv_ = std::move(litTexturedFragmentSpirvOpt.value());
  litTexturedVertexInputLayout_ = std::move(litTexturedLayoutOpt.value());

  // Step 2d (Plan 0023 Milestone 5): the fourth, MaterialKind::PbrDirectLit
  // built-in shader pair -- same shape as step 2c above, mirrored exactly.
  auto pbrDirectLitVertexSpirvOpt = loadSpirvFile(config.pbrDirectLitVertexShaderSpirvPath);
  auto pbrDirectLitFragmentSpirvOpt = loadSpirvFile(config.pbrDirectLitFragmentShaderSpirvPath);
  if (!pbrDirectLitVertexSpirvOpt.has_value() || !pbrDirectLitFragmentSpirvOpt.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shader SPIR-V from {} / {}", config.pbrDirectLitVertexShaderSpirvPath,
                        config.pbrDirectLitFragmentShaderSpirvPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto pbrDirectLitVertexReflectionResult = loadReflectionMetadata(config.pbrDirectLitVertexShaderReflectionPath);
  if (pbrDirectLitVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadReflectionMetadata() failed for {}", config.pbrDirectLitVertexShaderReflectionPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto pbrDirectLitLayoutOpt = pbrDirectLitVertexLayout(pbrDirectLitVertexReflectionResult.value());
  if (!pbrDirectLitLayoutOpt.has_value()) {
    ATLANTIS_LOG_ERROR(
        "pbrDirectLitVertexLayout(): reflected vertex-input attributes do not match the Vertex schema");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  pbrDirectLitVertexSpirv_ = std::move(pbrDirectLitVertexSpirvOpt.value());
  pbrDirectLitFragmentSpirv_ = std::move(pbrDirectLitFragmentSpirvOpt.value());
  pbrDirectLitVertexInputLayout_ = std::move(pbrDirectLitLayoutOpt.value());

  // Step 2e (Plan 0024 Milestone 6, ADR-0068 D-6): the fifth and sixth
  // built-in shader pairs -- the two output-transform variants -- same
  // shape as step 2d above, mirrored exactly, except the vertex layout
  // is resolved via outputTransformVertexLayout() (the fixed
  // fullscreen-triangle schema), not a MaterialKind-specific mesh
  // schema.
  auto outputTransformUnormVertexSpirvOpt = loadSpirvFile(config.outputTransformUnormVertexShaderSpirvPath);
  auto outputTransformUnormFragmentSpirvOpt = loadSpirvFile(config.outputTransformUnormFragmentShaderSpirvPath);
  if (!outputTransformUnormVertexSpirvOpt.has_value() || !outputTransformUnormFragmentSpirvOpt.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shader SPIR-V from {} / {}", config.outputTransformUnormVertexShaderSpirvPath,
                        config.outputTransformUnormFragmentShaderSpirvPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto outputTransformUnormVertexReflectionResult =
      loadReflectionMetadata(config.outputTransformUnormVertexShaderReflectionPath);
  if (outputTransformUnormVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadReflectionMetadata() failed for {}", config.outputTransformUnormVertexShaderReflectionPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto outputTransformUnormLayoutOpt = outputTransformVertexLayout(outputTransformUnormVertexReflectionResult.value());
  if (!outputTransformUnormLayoutOpt.has_value()) {
    ATLANTIS_LOG_ERROR(
        "outputTransformVertexLayout(): reflected vertex-input attributes do not match the fullscreen-triangle "
        "schema (unorm)");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  outputTransformUnormVertexSpirv_ = std::move(outputTransformUnormVertexSpirvOpt.value());
  outputTransformUnormFragmentSpirv_ = std::move(outputTransformUnormFragmentSpirvOpt.value());
  outputTransformUnormVertexInputLayout_ = std::move(outputTransformUnormLayoutOpt.value());

  auto outputTransformSrgbVertexSpirvOpt = loadSpirvFile(config.outputTransformSrgbVertexShaderSpirvPath);
  auto outputTransformSrgbFragmentSpirvOpt = loadSpirvFile(config.outputTransformSrgbFragmentShaderSpirvPath);
  if (!outputTransformSrgbVertexSpirvOpt.has_value() || !outputTransformSrgbFragmentSpirvOpt.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shader SPIR-V from {} / {}", config.outputTransformSrgbVertexShaderSpirvPath,
                        config.outputTransformSrgbFragmentShaderSpirvPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto outputTransformSrgbVertexReflectionResult =
      loadReflectionMetadata(config.outputTransformSrgbVertexShaderReflectionPath);
  if (outputTransformSrgbVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadReflectionMetadata() failed for {}", config.outputTransformSrgbVertexShaderReflectionPath);
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  auto outputTransformSrgbLayoutOpt = outputTransformVertexLayout(outputTransformSrgbVertexReflectionResult.value());
  if (!outputTransformSrgbLayoutOpt.has_value()) {
    ATLANTIS_LOG_ERROR(
        "outputTransformVertexLayout(): reflected vertex-input attributes do not match the fullscreen-triangle "
        "schema (srgb)");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::ShaderLoadFailed);
  }
  outputTransformSrgbVertexSpirv_ = std::move(outputTransformSrgbVertexSpirvOpt.value());
  outputTransformSrgbFragmentSpirv_ = std::move(outputTransformSrgbFragmentSpirvOpt.value());
  outputTransformSrgbVertexInputLayout_ = std::move(outputTransformSrgbLayoutOpt.value());

  // Step 3: Device.
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = config.applicationName, .enableValidationLayers = config.enableValidationLayers});
  if (deviceResult.isErr()) {
    ATLANTIS_LOG_ERROR("createDevice() failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::DeviceCreateFailed);
  }
  device_ = std::move(deviceResult.value());

  // Step 4: camera uniform Buffer. Plan 0019 Section P7: widened from
  // sizeof(float) * 32 (view + projection only) to also carry
  // FrameLightingData (176 bytes) appended immediately after, at
  // absolute byte offset 128 -- the one-time light-capture write (P9,
  // runFrame()) targets exactly this tail region. Plan 0023 Milestone 2
  // (ADR-0062's own Accepted Amendment): further widened with a
  // tail-only CameraWorldPositionData (16 bytes) appended after
  // FrameLightingData, at absolute byte offset 304 -- CameraMatrices
  // and FrameLightingData themselves stay byte-for-byte unmodified.
  auto cameraBufferResult = device_->createBuffer(
      {.purpose = BufferPurpose::Uniform,
       .sizeBytes = sizeof(float) * 32 + sizeof(FrameLightingData) + sizeof(CameraWorldPositionData)});
  if (cameraBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (camera uniform) failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::CameraBufferCreateFailed);
  }
  cameraBuffer_ = std::move(cameraBufferResult.value());

  // Step 4b (Plan 0024 Milestone 6, ADR-0068 D-6): the output-transform
  // pass's own fixed, never-scene-content fullscreen-triangle geometry
  // and Sampler -- format/extent-independent (unlike hdrColorTarget_/
  // outputTransform*Pipeline_ below), so created once here, alongside
  // cameraBuffer_ immediately above, never resized or recreated.
  // Mirrors createMesh()'s own createBuffer()+memcpy() pattern
  // (mesh.cpp) inline, since RuntimeApplication needs the vertex/index
  // Buffers as two separate members (Renderer::drawFrame()'s own
  // signature, Milestone 5), not bundled into a Mesh.
  const float fullscreenTriangleVertices[6] = {
      -1.0f, -1.0f,  // NDC (-1,-1)
      3.0f,  -1.0f,  // NDC (3,-1) -- deliberately off-screen, covers the full viewport with 3 vertices
      -1.0f, 3.0f,   // NDC (-1,3) -- deliberately off-screen
  };
  auto fullscreenTriangleVertexBufferResult = device_->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (fullscreen-triangle vertex) failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(
        RuntimeInitError::FullscreenTriangleVertexBufferCreateFailed);
  }
  fullscreenTriangleVertexBuffer_ = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fullscreenTriangleVertexBuffer_->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = device_->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (fullscreen-triangle index) failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(
        RuntimeInitError::FullscreenTriangleIndexBufferCreateFailed);
  }
  fullscreenTriangleIndexBuffer_ = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fullscreenTriangleIndexBuffer_->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult =
      device_->createSampler({.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) {
    ATLANTIS_LOG_ERROR("createSampler() (output-transform) failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::OutputTransformSamplerCreateFailed);
  }
  outputTransformSampler_ = std::move(outputTransformSamplerResult.value());

  // Step 4c (ADR-0068 D-4, correction discovered during Implementation
  // -- Human Review direction, chat, 2026-09-01): fallbackMaterial_'s
  // own Pipeline now targets the fixed HdrFormat::Rgba16Float, never
  // the caller's real, negotiated Format -- exactly like every other
  // geometry Pipeline (material_realization.cpp). It therefore no
  // longer has ANY dependency on a real swapchain/offscreen format,
  // and does not need to wait for the first SurfaceCreated the way it
  // used to: created once, here, alongside cameraBuffer_ above, fatal
  // on error (mirrors cameraBuffer_'s own identical treatment). D-4's
  // own text is explicit that geometry Pipelines -- fallbackMaterial_
  // included -- "no longer participate in format-change rebuild" at
  // all; the former per-format rebuild this member used to go through
  // (rebuildMaterialsForFormatChange(), Plan 0018 Section P13) is
  // retired together with this change -- see that function's own
  // removal, material_realization.h/.cpp.
  auto fallbackMaterialResult = createMaterial(
      *device_, {.vertexShader = {.spirvWords = vertexSpirv_.data(), .wordCount = vertexSpirv_.size()},
                 .fragmentShader = {.spirvWords = fragmentSpirv_.data(), .wordCount = fragmentSpirv_.size()},
                 .vertexInputLayout = vertexInputLayout_,
                 .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                 .depthFormat = DepthFormat::D32Sfloat,
                 .pushConstantSizeBytes = sizeof(float) * 16});
  if (fallbackMaterialResult.isErr()) {
    ATLANTIS_LOG_ERROR("createMaterial() (fallback) failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(RuntimeInitError::FallbackMaterialCreateFailed);
  }
  fallbackMaterial_ = std::make_unique<atlantis::renderer::Material>(std::move(fallbackMaterialResult.value()));

  // No hdrColorTarget_/outputTransform*Pipeline_ construction step
  // here -- Plan 0013 Section D6/Spec 0013's own Bootstrap Sequencing
  // Detail: no real swapchain format (or, for hdrColorTarget_, real
  // extent) is known before the first SurfaceCreated, so their own
  // first construction happens inside runFrame()'s own resize-/format-
  // change checks below -- exactly the code path that later handles
  // every subsequent resize/format change identically.

  // Step 5: scene load -- Plan 0015 Section D10, steps (a)-(g), factored
  // into loadAndInstantiateScene() (scene_load.h) so V20's own manifest/
  // artifact/dependency-unresolved failure paths are directly testable
  // without a real Device (see that header's own comment). Replaces the
  // former fixed, hardcoded six-entity validation scene (Plan 0014
  // Section D9) and its own single-asset load/Mesh-create steps: mesh
  // resolution and loading are now driven entirely by the scene's own
  // declared Renderable references and dependency manifest, not a
  // single hardcoded AssetId. Every early-return happens before world_/
  // meshResourceMap_ are ever touched -- both remain in their own
  // default, harmless states (std::nullopt / empty) on any failure
  // path, so RuntimeApplication never reaches Running with a partially-
  // published scene.
  auto sceneLoadResult = loadAndInstantiateScene(config, device_.get(), vertexInputLayout_);
  if (sceneLoadResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadAndInstantiateScene() failed");
    lifecycle_.markFailed();
    return atlantis::Result<std::monostate, RuntimeInitError>::Err(sceneLoadResult.error());
  }
  SceneLoadOutcome& outcome = sceneLoadResult.value();

  // Publish -- only now, both fully built (D10 step (g)). World is
  // move-constructible but NOT move-assignable (ADR-0049/Spec 0014,
  // unchanged) -- world_ is std::optional<World> and this is emplace(),
  // i.e. in-place move-CONSTRUCTION, never assignment. meshResourceMap_
  // is a plain std::unordered_map, whose own move-assignment is not
  // deleted, so plain assignment is correct there. Both steps are
  // proven noexcept at compile time by the two static_asserts above
  // this file's own anonymous namespace -- this two-step publish is
  // genuinely atomic in effect (if the first step runs, it cannot
  // throw, and the second cannot throw either), not merely assumed
  // safe; no catch/rollback exists here because none is needed.
  world_.emplace(std::move(outcome.world));
  meshResourceMap_ = std::move(outcome.meshResourceMap);
  materialDataMap_ = std::move(outcome.materialDataMap);
  textureDataMap_ = std::move(outcome.textureDataMap);

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

  // Format-change check (correction, ADR-0068 D-4, discovered during
  // Implementation -- Human Review direction, chat, 2026-09-01):
  // geometry Pipelines (fallbackMaterial_/materialResourceMap_) no
  // longer participate in format-change rebuild at all -- every one of
  // them targets the fixed HdrFormat::Rgba16Float, unconditionally,
  // created once at startup (fallbackMaterial_) or on first reference
  // (materialResourceMap_, via realizePendingMaterials(), Phase 2,
  // unaffected by which format is negotiated). Only the output-
  // transform Pipeline (D-6) still varies with the final target's real,
  // negotiated format -- built here, directly, a raw
  // Device::createPipeline() call (never a Material: no push constants,
  // no sampled-texture/sampler pair in the Material sense), and swapped
  // in only after this frame's own submit() call returns Ok (below),
  // reusing the exact real mechanism Plan 0018 Section P13 already
  // proves safe for this identical "prepare before draw, swap after
  // submit" shape (Device::submit()'s own internal
  // waitAndReleaseRetainedSubmission() has by then already confirmed
  // the PREVIOUS frame's GPU work -- the last work that could have
  // referenced the OLD output-transform Pipeline -- has finished).
  const atlantis::rhi::Format currentFormat = presentation_->metadata().format;
  std::optional<std::unique_ptr<atlantis::rhi::Pipeline>> pendingOutputTransformPipeline;
  bool formatRebuildFailedThisFrame = false;
  if (!lastSeenFormat_.has_value() || currentFormat != *lastSeenFormat_) {
    // isSrgbFormat() is the one place that decides which of the two
    // closed shader/Pipeline variants this Format needs -- the other is
    // untouched.
    const bool useSrgbVariant = isSrgbFormat(currentFormat);
    const atlantis::rhi::VertexInputLayout& outputTransformVertexLayoutRef =
        useSrgbVariant ? outputTransformSrgbVertexInputLayout_ : outputTransformUnormVertexInputLayout_;
    const std::vector<std::uint32_t>& outputTransformVertexSpirvRef =
        useSrgbVariant ? outputTransformSrgbVertexSpirv_ : outputTransformUnormVertexSpirv_;
    const std::vector<std::uint32_t>& outputTransformFragmentSpirvRef =
        useSrgbVariant ? outputTransformSrgbFragmentSpirv_ : outputTransformUnormFragmentSpirv_;
    auto outputTransformPipelineResult = device_->createPipeline(
        {.vertexShader = {.spirvWords = outputTransformVertexSpirvRef.data(),
                           .wordCount = outputTransformVertexSpirvRef.size()},
         .fragmentShader = {.spirvWords = outputTransformFragmentSpirvRef.data(),
                             .wordCount = outputTransformFragmentSpirvRef.size()},
         .vertexInputLayout = outputTransformVertexLayoutRef,
         .colorFormat = currentFormat,
         .hasSampledTextureBinding = true,
         .hasCameraUniformBinding = false,
         .hasDepthAttachment = false});
    if (outputTransformPipelineResult.isErr()) {
      ATLANTIS_LOG_ERROR(
          "createPipeline() (output-transform) failed during format-change rebuild -- keeping the existing "
          "outputTransform*Pipeline_ and retrying next frame");
      // lastSeenFormat_ intentionally NOT updated -- retry next frame.
      formatRebuildFailedThisFrame = true;
    } else {
      pendingOutputTransformPipeline = std::move(outputTransformPipelineResult.value());
      // lastSeenFormat_ is updated only once the swap itself actually
      // happens (after submit() succeeds, below) -- not here -- so a
      // subsequent submit() failure never leaves lastSeenFormat_ naming
      // a format outputTransform*Pipeline_ was never actually rebuilt
      // for.
    }
  }

  // Extent-change check: recreate the depth Texture and hdrColorTarget_
  // -- Pipeline is untouched (dynamic viewport/scissor). Plan 0024
  // Milestone 6: both resources share the SAME trigger and are
  // attempted independently, but lastSeenExtent_ is only ever updated
  // if BOTH succeed this frame -- if either fails, the other's own
  // freshly-created value (if any) is discarded via ordinary RAII
  // rather than partially adopted, so depthTexture_/hdrColorTarget_
  // never end up at two different extents and the shared trigger
  // correctly retries BOTH next frame, not just the one that failed.
  const Extent2D currentExtent = target->extent();
  if (!lastSeenExtent_.has_value() || !(currentExtent == *lastSeenExtent_)) {
    auto newTextureResult = device_->createTexture({.extent = currentExtent, .format = DepthFormat::D32Sfloat});
    auto newHdrColorTargetResult = device_->createHdrColorTarget({.extent = currentExtent});
    if (newTextureResult.isErr() || newHdrColorTargetResult.isErr()) {
      if (newTextureResult.isErr()) {
        ATLANTIS_LOG_ERROR(
            "createTexture() (depth) failed during resize -- keeping the existing depth Texture and retrying next "
            "frame");
      }
      if (newHdrColorTargetResult.isErr()) {
        ATLANTIS_LOG_ERROR(
            "createHdrColorTarget() failed during resize -- keeping the existing HdrColorTarget and retrying next "
            "frame");
      }
      // lastSeenExtent_ intentionally NOT updated -- retry both next frame.
    } else {
      depthTexture_ = std::move(newTextureResult.value());
      hdrColorTarget_ = std::move(newHdrColorTargetResult.value());
      lastSeenExtent_ = currentExtent;
    }
  }

  // Plan 0024 Milestone 6 (correction, ADR-0068 D-4): fallbackMaterial_
  // is now always valid once RuntimeApplication reaches Running --
  // created once at startup (initializeSteps()), never format-
  // dependent, never rebuilt -- so no effective-vs-stale distinction is
  // needed for it any more (contrast effectiveOutputTransformPipeline
  // below, which still has one, since IT is still format-dependent).
  //
  // effectiveOutputTransformPipeline: the just-built candidate this
  // frame if a rebuild succeeded, otherwise whichever of
  // outputTransformUnormPipeline_/...SrgbPipeline_ currently matches
  // lastSeenFormat_ -- but ONLY when this frame's own format-change
  // check did not just fail (formatRebuildFailedThisFrame): if it did,
  // currentFormat is BY CONSTRUCTION different from lastSeenFormat_
  // (that mismatch is what entered the format-change branch above), so
  // the stale, lastSeenFormat_-built Pipeline would not match this
  // frame's real target format either -- falling through to nullptr
  // (handled by the guard below, which skips the whole frame) is the
  // only safe outcome, not a reuse of a Pipeline built for a format
  // this frame's real target no longer has.
  atlantis::rhi::Pipeline* effectiveOutputTransformPipeline =
      pendingOutputTransformPipeline.has_value()
          ? pendingOutputTransformPipeline->get()
          : (!formatRebuildFailedThisFrame && lastSeenFormat_.has_value()
                 ? (isSrgbFormat(*lastSeenFormat_) ? outputTransformSrgbPipeline_.get()
                                                    : outputTransformUnormPipeline_.get())
                 : nullptr);

  if (!fallbackMaterial_ || !effectiveOutputTransformPipeline || !depthTexture_ || !hdrColorTarget_) {
    return;  // nothing valid to draw yet -- target dropped via RAII, no leaked GPU state
  }

  // Plan 0014 Section D8: World-driven extraction replaces the prior
  // fixed single-cube camera write + DrawItem build. Plan 0015 Section
  // D10: world_ is std::optional<World>, guaranteed populated here --
  // runFrame() is only ever called once RuntimeApplication has reached
  // Running, which only happens after initializeSteps() step (g) has
  // already published it (see world_'s own declaration comment,
  // runtime_application.h).
  world_->updateTransforms();

  const std::optional<atlantis::world::EntityId> activeCamera = world_->activeCamera();
  if (!activeCamera.has_value()) {
    // Every scene this Plan's own decodeScene()/ValidatedSceneData
    // pipeline accepts either has no active camera declared at all (an
    // authoring-time choice, not reachable for the one real scene this
    // Plan ships, D11) or one whose Camera presence is already
    // guaranteed by cook/decode-time validation (D4/D6) -- reaching
    // this path for a scene that DID declare one indicates a genuine
    // construction bug, the same "should never happen in correct
    // operation" category Spec 0013 already established for a second
    // SurfaceCreated/an unexpected SurfaceDestroyed.
    ATLANTIS_LOG_ERROR("runFrame(): World has no active camera");
    lifecycle_.markFailed();
    return;
  }
  activeCameraEntity_ = activeCamera;  // cached for logging only; World itself is the source of truth

  const auto cameraWorldMatrixResult = world_->getWorldMatrix(*activeCamera);
  ATLANTIS_CHECK_MSG(cameraWorldMatrixResult.isOk(),
                      "runFrame(): getWorldMatrix() failed for the handle activeCamera() just returned");
  const auto cameraComponentResult = world_->getCamera(*activeCamera);
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

  // Plan 0022 Section M1 (Spec 0022's own corrected design): every
  // successful frame re-extracts and publishes the complete, current
  // FrameLightingData from World's live state -- not a one-time
  // snapshot. Safe at this exact point without any new synchronization
  // primitive because it is the same, already-safe write point the
  // Camera write immediately above already occupies, downstream of
  // VulkanPresentation::acquireNextTarget()'s own pre-existing Step 0
  // drain (vulkan_presentation.cpp) and world_->updateTransforms()
  // (above) -- see Spec 0022's own "Corrected Motivation"/"Corrected
  // Design" sections for the full evidence. World::setLight()/
  // setLocalTransform()/setParent()/createEntity()/destroyEntity()
  // calls made against World are all reflected here on the next
  // successful frame reaching this point -- lightEntities()/getLight()
  // are live, uncached reads, and updateTransforms() above already
  // refreshes every entity's own cachedWorldMatrix unconditionally,
  // every frame, before this block runs.
  std::vector<LightExtractionInput> lightInputs;
  for (const atlantis::world::EntityId& id : world_->lightEntities()) {
    const auto lightResult = world_->getLight(id);
    const auto lightWorldMatrixResult = world_->getWorldMatrix(id);
    ATLANTIS_CHECK_MSG(lightResult.isOk() && lightWorldMatrixResult.isOk(),
                        "runFrame(): getLight()/getWorldMatrix() failed for a handle lightEntities() just returned");
    lightInputs.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  if (lightingResult.isErr()) {
    // extractFrameLightingData()'s own two real failure modes are not
    // the same kind of thing, precisely: (1) Err(DegenerateLightDirection)
    // -- what this branch actually handles -- is a genuine Result-based
    // return; the function builds its own complete, local
    // FrameLightingData value and returns it by value only on the Ok
    // path, so reaching this branch never touches cameraData, and no
    // mapped byte is ever partially overwritten. (2) too many lights of
    // one kind is a categorically different, fail-fast
    // ATLANTIS_CHECK_MSG abort (Debug and Release alike) inside
    // extractFrameLightingData() itself -- not a Result at all in
    // production; reaching it terminates the process before control
    // could ever return here, so it is likewise never a partial
    // cameraData write, but for a different reason (the process is
    // already gone, not that an Err was gracefully handled). Both
    // reachable-in-principle failures are, for different reasons, never
    // a partial write -- neither is a Result-based transactional
    // rollback of an in-flight write. A degenerate light Transform
    // reaching case (1) is an unrecoverable construction-bug indicator,
    // matching extractCameraMatrices()'s own identical "should never
    // happen" treatment at this same call site's own sibling check
    // above.
    ATLANTIS_LOG_ERROR("runFrame(): extractFrameLightingData() failed");
    lifecycle_.markFailed();
    return;
  }
  // reinterpret_cast through the already-obtained cameraData pointer
  // (Buffer::mappedData() is mapped once, at construction -- a second
  // call would return the identical pointer) rather than a second,
  // independent mappedData() call, keeping this write visibly,
  // textually anchored to the camera-portion write immediately above.
  // A full struct assignment -- all 176 bytes, every frame -- so a light
  // count that has decreased since the previous frame leaves no stale
  // trailing slot bytes (extractFrameLightingData() itself already
  // value-initializes a fresh result every call).
  auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
  *lightingData = lightingResult.value();

  // Plan 0023 Milestone 2 (ADR-0062's own Accepted Amendment): tail-only
  // CameraWorldPositionData write, unconditional every frame like the
  // camera-matrix and lighting writes immediately above -- appended
  // after FrameLightingData's own 176 bytes (44 floats), landing at
  // absolute byte offset 304 (32 + 44 = 76 floats in). Derived from the
  // same cameraWorldMatrix already extracted above for
  // extractCameraMatrices(), independently, via extractCameraWorldPosition().
  auto* cameraWorldPositionData = reinterpret_cast<CameraWorldPositionData*>(cameraData + 32 + 44);
  *cameraWorldPositionData = extractCameraWorldPosition(cameraWorldMatrixResult.value());

  // Plan 0015 Section D10: knownMeshAssetIds is meshResourceMap_'s own
  // key set, collected once per frame (not once per entity) -- passed
  // to resolveMeshAsset() for the membership check; meshResourceMap_
  // itself is then queried directly by AssetId for the actual Mesh,
  // exactly as scene_extraction.h's own updated resolveMeshAsset()
  // doc comment describes.
  std::vector<atlantis::asset_system::AssetId> knownMeshAssetIds;
  knownMeshAssetIds.reserve(meshResourceMap_.size());
  for (const auto& [assetId, mesh] : meshResourceMap_) knownMeshAssetIds.push_back(assetId);

  // Plan 0018 Section P12 (Spec 0018 D8 step 1): the pending set is a
  // pure function of current state, recomputed every frame. referencedMaterialIds
  // is collected from World's own already-deterministic
  // renderableEntities() iteration -- never an unordered_map -- so
  // realizePendingMaterials()'s own upload-pass recording order stays
  // reproducible frame-to-frame (Human Review Approval item 3).
  std::vector<atlantis::asset_system::AssetId> referencedMaterialIds;
  for (const atlantis::world::EntityId& id : world_->renderableEntities()) {
    const auto renderableResult = world_->getRenderable(id);
    ATLANTIS_CHECK_MSG(renderableResult.isOk(),
                        "runFrame(): getRenderable() failed for a handle renderableEntities() just returned");
    if (const auto& materialAsset = renderableResult.value().materialAsset; materialAsset.has_value()) {
      if (std::find(referencedMaterialIds.begin(), referencedMaterialIds.end(), *materialAsset) ==
          referencedMaterialIds.end()) {
        referencedMaterialIds.push_back(*materialAsset);
      }
    }
  }
  std::vector<atlantis::asset_system::AssetId> alreadyRealizedMaterialIds;
  alreadyRealizedMaterialIds.reserve(materialResourceMap_.size());
  for (const auto& [assetId, material] : materialResourceMap_) alreadyRealizedMaterialIds.push_back(assetId);
  const std::vector<atlantis::asset_system::AssetId> pendingMaterialIds =
      computePendingMaterialIds(referencedMaterialIds, alreadyRealizedMaterialIds);

  auto commandListResult = device_->createCommandList();
  if (commandListResult.isErr()) {
    ATLANTIS_LOG_ERROR("createCommandList() failed");
    lifecycle_.markFailed();
    return;
  }
  std::unique_ptr<atlantis::rhi::CommandList> commandList = std::move(commandListResult.value());

  // Plan 0018 Section P12 (Spec 0018 D8 steps 2-3): records any pending
  // materials' own upload passes into commandList BEFORE the draw graph
  // below -- exactly one CommandList, exactly one submit() covers both
  // (Pre-draft verification [Claim a]). Safe to call unconditionally,
  // even with an empty pendingMaterialIds (records nothing).
  std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizedCandidates =
      realizePendingMaterials(*device_, *commandList, unlitTexturedVertexInputLayout_, unlitTexturedVertexSpirv_,
                               unlitTexturedFragmentSpirv_, litTexturedVertexInputLayout_,
                               litTexturedVertexSpirv_, litTexturedFragmentSpirv_, pbrDirectLitVertexInputLayout_,
                               pbrDirectLitVertexSpirv_, pbrDirectLitFragmentSpirv_, pendingMaterialIds,
                               sampledTextureResourceMap_, materialDataMap_, textureDataMap_);
  // Plan 0018 Section P12 (Spec 0018 D8 step 5): gated on "at least one
  // material was newly realized this frame" -- NOT narrowed to "at least
  // one NEW TEXTURE was uploaded this frame". A realized candidate whose
  // own texture already exists (D10 dedup against a texture a DIFFERENT
  // material realized in an earlier frame) still creates a brand-new
  // Sampler/Pipeline this frame, bound into this frame's own
  // just-submitted CommandList; if it is not moved into the persistent
  // resource maps below, it is destroyed via ordinary RAII when
  // realizedCandidates goes out of scope at the end of this function --
  // unsafe without this frame's own waitIdle() (submit()'s own internal
  // drain only ever confirms the PREVIOUS frame's work, never this one),
  // and it would also mean this material is never actually cached
  // (computePendingMaterialIds() would find it "pending" again every
  // subsequent frame, forever).
  const bool anyMaterialRealizedThisFrame = !realizedCandidates.empty();

  // Plan 0024 Milestone 6 (correction, ADR-0068 D-4): formatRebuildFailedThisFrame
  // can no longer be true by the time execution reaches this point --
  // it only ever gates effectiveOutputTransformPipeline (above), and a
  // true value there resolves that pointer to nullptr, which the guard
  // above this point has already returned on. The former unconditional
  // "rebuild failure means nothing is safe to draw" override (Plan 0018
  // Section P13/Human Review Approval item 4) is retired along with it:
  // materials are no longer format-dependent, so a format-change event
  // (successful or not) has no bearing on which materials are safe to
  // resolve any more.
  std::vector<atlantis::asset_system::AssetId> knownMaterialIds = alreadyRealizedMaterialIds;
  for (const auto& [assetId, candidate] : realizedCandidates) knownMaterialIds.push_back(assetId);

  std::vector<DrawItem> drawItems;
  for (const atlantis::world::EntityId& id : world_->renderableEntities()) {
    const auto renderableResult = world_->getRenderable(id);
    ATLANTIS_CHECK_MSG(renderableResult.isOk(),
                        "runFrame(): getRenderable() failed for a handle renderableEntities() just returned");
    const auto resolveResult = resolveMeshAsset(renderableResult.value().meshAsset, knownMeshAssetIds);
    if (resolveResult.isErr()) {
      // Recoverable, per-entity: a single bad reference should not
      // halt an otherwise-valid scene, matching the general "keep
      // going, log, retry/skip" philosophy the existing extent-change
      // retry logic above already establishes.
      ATLANTIS_LOG_ERROR("runFrame(): resolveMeshAsset() could not resolve a Renderable entity's own AssetId");
      continue;
    }
    const auto worldMatrixResult = world_->getWorldMatrix(id);
    ATLANTIS_CHECK_MSG(worldMatrixResult.isOk(),
                        "runFrame(): getWorldMatrix() failed for a handle renderableEntities() just returned");

    // Plan 0018 Section P14 (Spec 0018 D4's three-state semantics):
    // absent -> fallbackMaterial_ (always valid once Running, Plan 0024
    // Milestone 6 correction); present -> a real, asset-sourced
    // Material, resolved through a priority chain (this frame's own
    // newly-realized candidates first, then the existing, already-
    // realized materialResourceMap_); present but resolved by neither
    // -> skip this entity for this frame only (never the fallback --
    // D4 case 3, distinct from case 1 by construction).
    const atlantis::renderer::Material* resolvedMaterial = fallbackMaterial_.get();
    if (const auto& materialAsset = renderableResult.value().materialAsset; materialAsset.has_value()) {
      const auto materialResolveResult = resolveMaterialAsset(*materialAsset, knownMaterialIds);
      if (materialResolveResult.isErr()) {
        ATLANTIS_LOG_ERROR(
            "runFrame(): resolveMaterialAsset() could not resolve a Renderable entity's own material AssetId -- "
            "skipping this entity for this frame only");
        continue;
      }

      // Plan 0019 Section P15: gated on this entity's OWN
      // MaterialAssetData.kind (already loaded by Phase 1, into
      // materialDataMap_, read here a second time -- never a new
      // load) -- an UnlitTextured-bound entity, or one with no
      // material at all (the untextured fallback path, handled
      // outside this if block entirely), never calls
      // checkConformalTransform(); its own world matrix may be
      // arbitrarily non-uniform-scaled or sheared with no effect.
      // Plan 0023 Milestone 8 (found and fixed during this Milestone's
      // own implementation, applying an already-Approved rule, not a
      // new design decision): PbrDirectLit shares this exact same
      // gating requirement -- pbr_direct_lit.slang's own vertexMain()
      // transforms the normal identically to lit_textured.slang's own
      // `mul((float3x3)pushConstants.objectToWorld, input.normal)`
      // (Milestone 4), so a non-conformal PbrDirectLit-bound entity's
      // own world normal would silently be wrong with no defensive
      // skip if left out of this gate, exactly the defect class Spec
      // 0019 D7/P15 already fixed for LitTextured.
      const auto materialDataIt = materialDataMap_.find(*materialAsset);
      ATLANTIS_CHECK_MSG(materialDataIt != materialDataMap_.end(),
                          "runFrame(): a resolveMaterialAsset()-confirmed material AssetId must already be a key "
                          "in materialDataMap_ (Phase 1 load)");
      if (materialDataIt->second.kind == atlantis::asset_system::MaterialKind::LitTextured ||
          materialDataIt->second.kind == atlantis::asset_system::MaterialKind::PbrDirectLit) {
        const auto conformalResult = checkConformalTransform(worldMatrixResult.value());
        if (conformalResult.isErr()) {
          // Recoverable, per-entity, per-frame -- never scene-load-
          // fatal, matching this loop's own established "keep going,
          // log, skip" philosophy for a bad mesh/material reference.
          ATLANTIS_LOG_ERROR(
              "runFrame(): a LitTextured- or PbrDirectLit-bound entity's own world matrix failed "
              "checkConformalTransform() -- skipping this entity for this frame only");
          continue;
        }
      }

      resolvedMaterial = nullptr;
      if (const auto it = realizedCandidates.find(*materialAsset); it != realizedCandidates.end()) {
        resolvedMaterial = it->second.material.get();
      }
      if (!resolvedMaterial) {
        const auto it = materialResourceMap_.find(*materialAsset);
        if (it != materialResourceMap_.end()) resolvedMaterial = it->second.get();
      }
      ATLANTIS_CHECK_MSG(resolvedMaterial != nullptr,
                          "runFrame(): resolveMaterialAsset() succeeded but no source map actually had the id -- "
                          "knownMaterialIds must be out of sync with its own sources");
    }

    DrawItem item;
    item.mesh = &meshResourceMap_.at(renderableResult.value().meshAsset);
    item.material = resolvedMaterial;
    item.objectToWorld = worldMatrixResult.value();
    drawItems.push_back(item);
  }

  renderer_.drawFrame(*commandList, *target, *depthTexture_, *cameraBuffer_, drawItems,
                       atlantis::rhi::ResourceState::PresentSource, *hdrColorTarget_, *fullscreenTriangleVertexBuffer_,
                       *fullscreenTriangleIndexBuffer_, *effectiveOutputTransformPipeline, *outputTransformSampler_);

  auto submitResult = device_->submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    ATLANTIS_LOG_ERROR("submit() failed");
    static_cast<void>(classifySubmitError(submitResult.error()));
    lifecycle_.markFailed();
    return;
  }

  // Plan 0018 Section P13 (Human Review Approval item 2), narrowed by
  // the Plan 0024 Milestone 6 correction above to the ONE resource that
  // is still format-dependent: only now, after submit() has returned
  // Ok -- meaning Device::submit()'s own internal
  // waitAndReleaseRetainedSubmission() has already confirmed the
  // PREVIOUS frame's GPU work has finished -- is it safe to swap in the
  // rebuilt output-transform Pipeline candidate; the OLD Pipeline this
  // overwrites is destroyed here, provably safe.
  if (pendingOutputTransformPipeline.has_value()) {
    // Plan 0024 Milestone 6 (ADR-0068 D-6): isSrgbFormat(currentFormat)
    // selects which one of outputTransformUnormPipeline_/
    // ...SrgbPipeline_ this rebuild actually targeted -- the other is
    // untouched, matching the exact selection
    // pendingOutputTransformPipeline was itself built against, above.
    if (isSrgbFormat(currentFormat)) {
      outputTransformSrgbPipeline_ = std::move(*pendingOutputTransformPipeline);
    } else {
      outputTransformUnormPipeline_ = std::move(*pendingOutputTransformPipeline);
    }
    lastSeenFormat_ = currentFormat;
  }

  // Plan 0018 Section P12 (Spec 0018 D8 step 5): this extra CPU stall is
  // paid on any frame that newly realized at least one material -- not
  // narrowed to "newly uploaded a texture" (see anyMaterialRealizedThisFrame's
  // own comment above) -- never on an ordinary frame, and never merely
  // because a format rebuild also happened this frame (that hazard is
  // already fully closed by submit()'s own internal drain above, needing
  // no further wait). Blocks until this frame's own upload+draw work has
  // finished, at which point every staging Buffer this frame's own
  // realizedCandidates created is safe to destroy (via ordinary RAII,
  // when realizedCandidates itself goes out of scope at the end of this
  // function) and the SubmissionSignal present() receives below already
  // wraps an already-signaled VkSemaphore (Pre-draft verification
  // [Claim d]).
  if (anyMaterialRealizedThisFrame) {
    auto waitResult = device_->waitIdle();
    if (waitResult.isErr()) {
      ATLANTIS_LOG_ERROR("waitIdle() failed after a realization frame's own submit() -- treated as fatal, matching "
                          "shutdown()'s own identical severity for an unrecoverable wait");
      lifecycle_.markFailed();
      return;
    }
    // Only after waitIdle() succeeds does a newly-realized material
    // become visible to any FUTURE frame's own DrawItem resolution --
    // this frame's own drawItems above already used the local
    // candidates directly (D8 step 3's "visible the very frame it
    // succeeds" guarantee), so this publish is for frame N+1 onward.
    for (auto& [assetId, candidate] : realizedCandidates) {
      if (candidate.newSampledTexture) {
        sampledTextureResourceMap_.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
      }
      samplerResourceMap_.emplace(assetId, std::move(candidate.sampler));
      materialResourceMap_.emplace(assetId, std::move(candidate.material));
    }
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

  // Plan 0018 Section P10: explicit clear, texture/sampler-before-
  // material order (D9) -- material_.reset()'s own former single line
  // widens to four, in this exact order, matching
  // meshResourceMap_.clear()'s own existing explicit-clear precedent.
  // Plan 0024 Milestone 6: widened again with every new HDR-related
  // member, each reset in the exact reverse order of its own
  // declaration (runtime_application.h) -- the same, no-new-rule
  // discipline this whole sequence already follows.
  outputTransformSrgbPipeline_.reset();
  outputTransformUnormPipeline_.reset();
  outputTransformSampler_.reset();
  fullscreenTriangleIndexBuffer_.reset();
  fullscreenTriangleVertexBuffer_.reset();
  fallbackMaterial_.reset();
  materialResourceMap_.clear();
  samplerResourceMap_.clear();
  sampledTextureResourceMap_.clear();
  hdrColorTarget_.reset();
  depthTexture_.reset();
  cameraBuffer_.reset();
  meshResourceMap_.clear();
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

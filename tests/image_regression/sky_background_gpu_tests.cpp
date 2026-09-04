// Plan 0026 Milestone 7 (ADR-0071): real-GPU pixel tests for the visible
// sky background -- visibility with no scene geometry, full occlusion by
// opaque geometry, direction correctness under camera rotation (verified
// against the studio environment's own known, analytically-generated key
// light direction -- not merely "the image changed"), and translation
// invariance. Reuses IblMaterialDemoFixture (a type alias to
// PbrMaterialDemoFixture, Spec 0025/Plan 0026 M5's own established
// environment+sky wiring) directly: one full renderIblMaterialDemoFrame()
// call publishes the environment through its own real path, then every
// TEST_CASE below drives Renderer::drawFrame() itself with a caller-
// controlled camera and DrawItem set, borrowing the fixture's
// already-realized resources. Every GPU command is recorded by
// Renderer::drawFrame()'s own internal RenderGraph and one further
// RenderGraphBuilder/execute() copy pass -- mirroring
// headless_rendering_gpu_tests.cpp's own established draw-then-copy
// sequence exactly; no CommandList call is ever issued outside a
// compiled RenderGraph pass callback.

#include "fixture/ibl_material_demo_fixture.h"

#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using atlantis::image_regression::IblMaterialDemoFixture;
using atlantis::image_regression::kIblMaterialDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::CommandList;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::Mat4;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Duplicated, not shared -- matches ibl_material_demo_gpu_tests.cpp's own
// buildIblConfig(), which is anonymous-namespace-scoped in that .cpp and
// not exported. This repository's own established "each test file owns
// its small fixture config" precedent.
atlantis::runtime::BootstrapConfig buildIblConfig() {
  atlantis::runtime::BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_ibl_material_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH;
  const std::string unlit = ATLANTIS_IBL_DEMO_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_IBL_DEMO_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_IBL_DEMO_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_IBL_DEMO_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  const std::string sky = ATLANTIS_IBL_DEMO_SKY_SHADER_DIR;
  config.skyVertexShaderSpirvPath = sky + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = sky + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = sky + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = sky + "/sky.frag.refl.json";
  const std::string output = ATLANTIS_IBL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_IBL_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_IBL_DEMO_ENVIRONMENT_METADATA_PATH;
  return config;
}

// assets/environments/generate_ibl_studio_source.ps1's own key-light lobe
// direction constant, verbatim (the brightest, most tightly-falling-off
// of the three lobes -- exponent 9.0, falloff scale 0.0022, vs. fill's
// 2.6/0.0060 and rim's 4.0/0.0035). A camera looking exactly along this
// direction samples at the lobe's own peak; looking the opposite way
// samples deep into its near-zero tail -- a real, independently-derived
// directional prediction, not merely "some difference."
constexpr float kKeyDirX = -0.6047079f;
constexpr float kKeyDirY = 0.3527463f;
constexpr float kKeyDirZ = 0.7054926f;

constexpr float kFovYRadians = 1.0471975512f;  // 60 degrees
constexpr float kNearZ = 0.1f;
constexpr float kFarZ = 100.0f;

// column2 (indices 8,9,10) is the only basis column extractCameraMatrices()
// reads (its own forward = normalize(-column2)); column3 (12,13,14) is
// the only other column it reads (eye position). Columns 0/1 are unused
// by that function and left zero -- confirmed against its own real
// implementation (scene_extraction.cpp), not assumed. Used only where
// eye stays (0,0,0) on every call (the direction test below) --
// extractCameraMatrices()'s own lookAtMatrix() internally reconstructs
// forward as normalize((eye + forward) - eye), which is exact only when
// eye is exactly zero; a nonzero eye of any real magnitude loses low bits
// of forward there (see writeCameraDirect() below for the case that needs
// a nonzero eye).
Mat4 cameraWorldMatrixFacing(float dirX, float dirY, float dirZ) {
  Mat4 m{};
  m[8] = -dirX;
  m[9] = -dirY;
  m[10] = -dirZ;
  m[15] = 1.0f;
  return m;
}

// Writes view/projection into the fixture's own 464-byte camera buffer's
// leading 128 bytes -- the only region sky.slang's CameraUniform reads
// (P2). The trailing lighting/SH region is left at whatever
// renderIblMaterialDemoFrame()'s own prior call already wrote there;
// sky.slang never reads it.
void writeCamera(IblMaterialDemoFixture& fixture, const Mat4& cameraWorldMatrix) {
  const auto extraction = extractCameraMatrices(cameraWorldMatrix, kFovYRadians, kNearZ, kFarZ, 1.0f);
  REQUIRE(extraction.isOk());
  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = extraction.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = extraction.value().projection[i];
}

// True identity view AND projection, written directly -- NOT routed
// through extractCameraMatrices() (which builds a real lookAt/perspective
// pair from its own input, never a pass-through identity regardless of
// what is fed to it). With both matrices identity, minimal_mesh's own
// vertexMain() (mul(projection, mul(view, mul(objectToWorld, position))))
// reduces to a direct pass-through, matching
// pipeline_depth_write_gpu_tests.cpp's own established convention (Plan
// 0026 Milestone 1) exactly: object-space xyz becomes clip-space
// directly, so a quad spanning NDC [-1,1] covers the whole screen.
void writeIdentityCamera(IblMaterialDemoFixture& fixture) {
  constexpr Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = identity[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = identity[i];
}

// Duplicated -- not shared -- from scene_extraction.cpp's own private
// lookAtMatrix()/perspectiveMatrix(), matching that file's own explicit
// "duplicated, not shared" precedent for this exact kind of small camera-
// math helper. Takes an already-normalized forward direction directly
// (never reconstructed from an eye+target round trip), so two calls
// sharing the same (fx,fy,fz) produce bit-identical rotation blocks
// regardless of eye -- the translation-invariance test below needs
// exactly this guarantee, which extractCameraMatrices()'s own eye+forward
// reconstruction cannot give for a nonzero eye (see cameraWorldMatrixFacing()'s
// own comment).
Mat4 lookAtMatrixFromForward(float fx, float fy, float fz, float eyeX, float eyeY, float eyeZ) {
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

  Mat4 result{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
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

Mat4 perspectiveMatrixDirect(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

// Writes a view/projection pair built directly from a fixed forward
// direction and an eye position -- bypassing extractCameraMatrices()'s
// own eye+forward reconstruction entirely (see lookAtMatrixFromForward()'s
// own comment for why that matters here).
void writeCameraDirect(IblMaterialDemoFixture& fixture, float fx, float fy, float fz, float eyeX, float eyeY,
                        float eyeZ) {
  const Mat4 view = lookAtMatrixFromForward(fx, fy, fz, eyeX, eyeY, eyeZ);
  const Mat4 projection = perspectiveMatrixDirect(kFovYRadians, 1.0f, kNearZ, kFarZ);
  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];
}

// One full Renderer::drawFrame() + copy-to-readback cycle against the
// fixture's own already-realized resources -- mirrors
// headless_rendering_gpu_tests.cpp's own established draw-then-copy
// RenderGraph sequence (one CommandList, one submit()) exactly. skyOn
// selects fixture.skyPipeline.get() or nullptr; drawItems is caller-
// supplied (empty for the direction/visibility/translation cases, one
// fullscreen quad for the occlusion case).
PixelBuffer renderOnce(IblMaterialDemoFixture& fixture, std::span<const DrawItem> drawItems, bool skyOn) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  auto commandListResult = fixture.device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  Renderer renderer;
  const atlantis::renderer::EnvironmentLighting lightingView = fixture.environmentLightingResources->borrowedView();
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                      rhi::ResourceState::TransferSource, *fixture.hdrColorTarget,
                      *fixture.fullscreenTriangleVertexBuffer, *fixture.fullscreenTriangleIndexBuffer,
                      *fixture.outputTransformPipeline, *fixture.outputTransformSampler, &lightingView,
                      skyOn ? fixture.skyPipeline.get() : nullptr);

  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  REQUIRE(copyCompileResult.isOk());
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());
  REQUIRE(fixture.device->waitIdle().isOk());

  PixelBuffer result;
  result.width = kIblMaterialDemoExtentPixels;
  result.height = kIblMaterialDemoExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kIblMaterialDemoExtentPixels) * kIblMaterialDemoExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();
  return result;
}

std::array<std::uint8_t, 4> centerPixel(const PixelBuffer& frame) {
  const std::uint32_t x = frame.width / 2;
  const std::uint32_t y = frame.height / 2;
  const std::size_t offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
  return {frame.rgba8[offset], frame.rgba8[offset + 1], frame.rgba8[offset + 2], frame.rgba8[offset + 3]};
}

int luminance(const std::array<std::uint8_t, 4>& pixel) {
  return static_cast<int>(pixel[0]) + static_cast<int>(pixel[1]) + static_cast<int>(pixel[2]);
}

// Catch2's own console reporter stringifies both operands of a failed
// comparison for its expansion message -- with two ~1 MB PixelBuffer::rgba8
// vectors (512x512x4 bytes) directly inside REQUIRE/CHECK, building and
// word-wrapping that string crashes Catch2 itself
// (catch_textflow.cpp's own "m_it != m_string->begin()" assertion) rather
// than reporting a real failure. Comparing a byte count instead keeps the
// check itself real and exact while keeping any failure message short.
std::size_t firstDifferingByte(const PixelBuffer& a, const PixelBuffer& b) {
  const std::size_t count = std::min(a.rgba8.size(), b.rgba8.size());
  for (std::size_t i = 0; i < count; ++i) {
    if (a.rgba8[i] != b.rgba8[i]) return i;
  }
  return a.rgba8.size() == b.rgba8.size() ? count : count + 1;  // count+1 signals a size mismatch, never a real index
}

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

struct Vertex {
  float position[3];
  float color[3];
};

[[nodiscard]] std::optional<VertexInputLayout> minimalMeshVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

}  // namespace

TEST_CASE("Visible sky background: visibility, occlusion, direction, and translation invariance (Plan 0026 M7)",
          "[image_regression][gpu][sky]") {
  auto fixtureResult = atlantis::image_regression::setUpIblMaterialDemoFixture(buildIblConfig());
  REQUIRE(fixtureResult.isOk());
  IblMaterialDemoFixture fixture = std::move(fixtureResult.value());
  REQUIRE(fixture.skyPipeline != nullptr);

  // Publishes environmentLightingResources through the fixture's own
  // real, established first-frame realize/upload/wait path (Spec 0025
  // M7) -- this TEST_CASE never duplicates that logic itself.
  auto primeResult = atlantis::image_regression::renderIblMaterialDemoFrame(fixture);
  REQUIRE(primeResult.isOk());
  REQUIRE(fixture.environmentLightingResources.has_value());

  const Mat4 facingKey = cameraWorldMatrixFacing(kKeyDirX, kKeyDirY, kKeyDirZ);
  const Mat4 facingAway = cameraWorldMatrixFacing(-kKeyDirX, -kKeyDirY, -kKeyDirZ);

  SECTION("Sky is visible with no DrawItems present") {
    writeCamera(fixture, facingKey);
    const PixelBuffer withSky = renderOnce(fixture, {}, /*skyOn=*/true);
    const PixelBuffer withoutSky = renderOnce(fixture, {}, /*skyOn=*/false);
    CHECK(centerPixel(withSky) != centerPixel(withoutSky));
  }

  SECTION("A fullscreen opaque DrawItem fully occludes the sky") {
    // Identity camera (matching pipeline_depth_write_gpu_tests.cpp's own
    // established convention): output.position == float4(input.position,
    // 1.0) directly, so a quad spanning NDC [-1,1]x[-1,1] at a near depth
    // covers every pixel regardless of which world direction the sky
    // itself would otherwise be sampling.
    constexpr Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    writeIdentityCamera(fixture);

    const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
    const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());
    const auto vertexReflection = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
    REQUIRE(vertexReflection.isOk());
    const auto vertexLayout = minimalMeshVertexLayout(vertexReflection.value());
    REQUIRE(vertexLayout.has_value());

    auto materialResult = createMaterial(
        *fixture.device,
        {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
         .vertexInputLayout = *vertexLayout,
         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16});
    REQUIRE(materialResult.isOk());
    Material material = std::move(materialResult.value());

    constexpr Vertex kQuadVertices[4] = {
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 0.9f, 0.1f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 0.9f, 0.1f}},
        {{-1.0f, 1.0f, 0.0f}, {1.0f, 0.9f, 0.1f}},
        {{1.0f, 1.0f, 0.0f}, {1.0f, 0.9f, 0.1f}},
    };
    constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};
    auto meshResult = createMesh(*fixture.device, *vertexLayout, kQuadVertices, sizeof(kQuadVertices), kQuadIndices, 6);
    REQUIRE(meshResult.isOk());
    Mesh mesh = std::move(meshResult.value());

    DrawItem item{.mesh = &mesh, .material = &material, .objectToWorld = identity};
    const std::vector<DrawItem> drawItems{item};

    const PixelBuffer withSky = renderOnce(fixture, drawItems, /*skyOn=*/true);
    const PixelBuffer withoutSky = renderOnce(fixture, drawItems, /*skyOn=*/false);
    // Full-screen coverage: the sky is never sampled at all once the
    // opaque quad depth-tests/writes over it, so the two renders must be
    // byte-identical, not merely similar.
    const std::size_t firstDiff = firstDifferingByte(withSky, withoutSky);
    INFO("first differing byte index (rgba8.size() = " << withSky.rgba8.size() << ") = " << firstDiff);
    CHECK(firstDiff == withSky.rgba8.size());
  }

  SECTION("Camera rotation changes the sky's sampled direction as predicted by the environment's own key light") {
    writeCamera(fixture, facingKey);
    const PixelBuffer facingKeyFrame = renderOnce(fixture, {}, /*skyOn=*/true);

    writeCamera(fixture, facingAway);
    const PixelBuffer facingAwayFrame = renderOnce(fixture, {}, /*skyOn=*/true);

    const int keyLuminance = luminance(centerPixel(facingKeyFrame));
    const int awayLuminance = luminance(centerPixel(facingAwayFrame));
    INFO("facing-key center pixel luminance (sum of R+G+B) = " << keyLuminance);
    INFO("facing-away center pixel luminance (sum of R+G+B) = " << awayLuminance);
    // A camera looking directly at the key light's own peak direction
    // must be markedly brighter than one looking directly away from it,
    // into the environment's own low-level ambient floor/ceiling term --
    // a real, direction-specific prediction derived from
    // generate_ibl_studio_source.ps1's own formula, confirmed against a
    // real capture before this threshold was fixed (see this file's own
    // commit message).
    CHECK(keyLuminance > awayLuminance + 60);
  }

  SECTION("Pure camera translation does not change the sky") {
    // writeCameraDirect() (not writeCamera()/extractCameraMatrices()) for
    // both calls -- passing the identical (kKeyDirX, kKeyDirY, kKeyDirZ)
    // forward literal both times guarantees a bit-identical view-matrix
    // rotation block regardless of eye, which the sky shader alone reads
    // (P2). extractCameraMatrices()'s own eye+forward reconstruction
    // cannot give this guarantee for a nonzero eye (see
    // cameraWorldMatrixFacing()'s own comment) -- confirmed by a real
    // capture that failed this exact check before this fix (see this
    // file's own commit message).
    writeCameraDirect(fixture, kKeyDirX, kKeyDirY, kKeyDirZ, 0.0f, 0.0f, 0.0f);
    const PixelBuffer atOrigin = renderOnce(fixture, {}, /*skyOn=*/true);

    writeCameraDirect(fixture, kKeyDirX, kKeyDirY, kKeyDirZ, 37.0f, -19.0f, 52.0f);
    const PixelBuffer translated = renderOnce(fixture, {}, /*skyOn=*/true);

    const std::size_t firstDiff = firstDifferingByte(atOrigin, translated);
    INFO("first differing byte index (rgba8.size() = " << atOrigin.rgba8.size() << ") = " << firstDiff);
    CHECK(firstDiff == atOrigin.rgba8.size());
  }
}

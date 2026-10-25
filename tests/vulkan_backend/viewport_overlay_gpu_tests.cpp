#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/renderer/viewport_overlay.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Plan 0066 M3b (Spec 0066 R3, R4; ADR-0132 D3; J7): the Viewport overlay's
// pixels, with fatal validation, on a small scene rendered offscreen -- pixel
// assertions, no golden. A frame is: a scene graph (the minimal_mesh pair,
// writing depth, as drawFrame() would leave the target and depth), then
// Renderer::drawViewportOverlay() continuing both from the scene graph's
// reported end states (ADR-0133 D5), then a copy graph. One CommandList, one
// submit() per frame. Pixel (x, y) is NDC ((x + 0.5) / 32 - 1, (y + 0.5) / 32 - 1)
// on the 64 x 64 target.

namespace {

using atlantis::renderer::DrawItem;
using atlantis::renderer::FrameEndStates;
using atlantis::renderer::Mesh;
using atlantis::renderer::ViewportOverlayAnchor;
using atlantis::renderer::ViewportOverlayCamera;
using atlantis::renderer::ViewportOverlayDepth;
using atlantis::renderer::ViewportOverlayDrawList;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::CommandList;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTarget;
using atlantis::rhi::Pipeline;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::ResourceState;
using atlantis::rhi::VertexAttributeFormat;
using atlantis::rhi::VertexInputLayout;
using Pixel = std::array<std::uint8_t, 4>;
using Matrix = std::array<float, 16>;

constexpr Extent2D kExtent{64, 64};
constexpr Format kColorFormat = Format::Rgba8Unorm;
constexpr Matrix kIdentity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<float, 4> kRed{1, 0, 0, 1};
constexpr std::array<float, 4> kGreen{0, 1, 0, 1};
constexpr std::array<float, 4> kBlue{0, 0, 1, 1};
constexpr Pixel kBlack{0, 0, 0, 255};
constexpr Pixel kRedPixel{255, 0, 0, 255};
constexpr Pixel kGreenPixel{0, 255, 0, 255};
constexpr Pixel kBluePixel{0, 0, 255, 255};

// Each test file owns its small fixture (pipeline_depth_write_gpu_tests.cpp's precedent).
[[nodiscard]] std::vector<std::uint32_t> loadSpirv(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  REQUIRE(file.is_open());
  const std::streamsize sizeBytes = file.tellg();
  REQUIRE((sizeBytes > 0 && sizeBytes % 4 == 0));
  file.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  REQUIRE(static_cast<bool>(file.read(reinterpret_cast<char*>(words.data()), sizeBytes)));
  return words;
}

// minimal_mesh's vertex: a position and a colour.
struct SceneVertex {
  float position[3];
  float color[3];
};
const VertexInputLayout kSceneLayout{
    .strideBytes = sizeof(SceneVertex),
    .attributes = {{.location = 0, .offsetBytes = offsetof(SceneVertex, position), .format = VertexAttributeFormat::Float3},
                   {.location = 1, .offsetBytes = offsetof(SceneVertex, color), .format = VertexAttributeFormat::Float3}}};
// The mask reads only the position, through the scene meshes' stride (as the shadow pass does).
const VertexInputLayout kMaskLayout{
    .strideBytes = sizeof(SceneVertex),
    .attributes = {{.location = 0, .offsetBytes = offsetof(SceneVertex, position), .format = VertexAttributeFormat::Float3}}};
const VertexInputLayout kFullscreenLayout{
    .strideBytes = sizeof(float) * 2, .attributes = {{.location = 0, .offsetBytes = 0, .format = VertexAttributeFormat::Float2}}};

constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

// An axis-aligned quad over NDC [minX, maxX] x [minY, maxY] at depth z.
[[nodiscard]] Mesh makeQuad(Device& device, float minX, float maxX, float minY, float maxY, float z,
                            const std::array<float, 3>& rgb) {
  const SceneVertex vertices[4] = {{{minX, minY, z}, {rgb[0], rgb[1], rgb[2]}},
                                   {{maxX, minY, z}, {rgb[0], rgb[1], rgb[2]}},
                                   {{minX, maxY, z}, {rgb[0], rgb[1], rgb[2]}},
                                   {{maxX, maxY, z}, {rgb[0], rgb[1], rgb[2]}}};
  auto mesh = atlantis::renderer::createMesh(device, kSceneLayout, vertices, sizeof(vertices), kQuadIndices, 6);
  REQUIRE(mesh.isOk());
  return std::move(mesh.value());
}

// A right-handed perspective, depth in [0, 1], column-major, looking down -Z.
[[nodiscard]] Matrix perspective(float fovYRadians, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Matrix m{};
  m[0] = f;
  m[5] = f;
  m[10] = farZ / (nearZ - farZ);
  m[11] = -1.0f;
  m[14] = nearZ * farZ / (nearZ - farZ);
  return m;
}

struct Fixture {
  std::unique_ptr<Device> device;
  std::unique_ptr<Pipeline> scene;
  std::unique_ptr<Pipeline> tested;
  std::unique_ptr<Pipeline> onTop;
  std::unique_ptr<Pipeline> mask;
  std::unique_ptr<Pipeline> outline;
  std::unique_ptr<atlantis::rhi::Buffer> sceneCamera;
  std::unique_ptr<atlantis::rhi::Buffer> overlayCamera;
  std::unique_ptr<atlantis::rhi::Buffer> vertices;
  std::unique_ptr<atlantis::rhi::Buffer> indices;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenVertices;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenIndices;
  std::unique_ptr<atlantis::rhi::Texture> depth;
  std::unique_ptr<OffscreenTarget> target;
  std::unique_ptr<OffscreenTarget> maskTarget;
  std::unique_ptr<atlantis::rhi::Sampler> sampler;
  std::unique_ptr<atlantis::rhi::Buffer> readback;

  Fixture() {
    auto created = atlantis::vulkan_backend::createDevice(
        {.applicationName = "Atlantis Viewport Overlay GPU Test", .enableValidationLayers = true});
    REQUIRE(created.isOk());
    device = std::move(created.value());

    const auto pipeline = [&](const char* name, const VertexInputLayout& layout, std::size_t pushBytes, bool camera,
                              std::uint32_t samplers, bool depthAttachment, bool depthWrite,
                              atlantis::rhi::ColorBlendMode blend) {
      const auto vertex = loadSpirv((std::string("shaders/") + name + ".vert.spv").c_str());
      const auto fragment = loadSpirv((std::string("shaders/") + name + ".frag.spv").c_str());
      auto result = device->createPipeline({.vertexShader = {.spirvWords = vertex.data(), .wordCount = vertex.size()},
                                            .fragmentShader = {.spirvWords = fragment.data(), .wordCount = fragment.size()},
                                            .vertexInputLayout = layout,
                                            .colorFormat = kColorFormat,
                                            .depthFormat = DepthFormat::D32Sfloat,
                                            .pushConstantSizeBytes = pushBytes,
                                            .sampledTextureBindingCount = samplers,
                                            .hasCameraUniformBinding = camera,
                                            .hasDepthAttachment = depthAttachment,
                                            .depthWriteEnabled = depthWrite,
                                            .colorBlendMode = blend});
      REQUIRE(result.isOk());
      return std::move(result.value());
    };
    using atlantis::rhi::ColorBlendMode;
    namespace r = atlantis::renderer;
    scene = pipeline("minimal_mesh", kSceneLayout, 64, true, 0, true, true, ColorBlendMode::Disabled);
    tested = pipeline("viewport_overlay", r::viewportOverlayVertexInputLayout(), r::kViewportOverlayPushConstantSizeBytes,
                      true, 0, true, false, ColorBlendMode::AlphaBlend);
    onTop = pipeline("viewport_overlay", r::viewportOverlayVertexInputLayout(), r::kViewportOverlayPushConstantSizeBytes,
                     true, 0, false, false, ColorBlendMode::AlphaBlend);
    mask = pipeline("selection_mask", kMaskLayout, 64, true, 0, false, false, ColorBlendMode::Disabled);
    outline = pipeline("selection_outline", kFullscreenLayout, r::kSelectionOutlinePushConstantSizeBytes, false, 1, false,
                       false, ColorBlendMode::Disabled);

    const auto buffer = [&](BufferPurpose purpose, std::size_t bytes,
                            atlantis::rhi::IndexType indexType = atlantis::rhi::IndexType::Uint16) {
      auto result = device->createBuffer({.purpose = purpose, .sizeBytes = bytes, .indexType = indexType});
      REQUIRE(result.isOk());
      return std::move(result.value());
    };
    sceneCamera = buffer(BufferPurpose::Uniform, sizeof(float) * 32);
    overlayCamera = buffer(BufferPurpose::Uniform, r::kViewportOverlayCameraSizeBytes);
    vertices = buffer(BufferPurpose::Vertex, 256 * sizeof(r::ViewportOverlayVertex));
    indices = buffer(BufferPurpose::Index, 512 * sizeof(std::uint32_t), atlantis::rhi::IndexType::Uint32);
    fullscreenVertices = buffer(BufferPurpose::Vertex, sizeof(float) * 6);
    fullscreenIndices = buffer(BufferPurpose::Index, sizeof(std::uint16_t) * 3);
    const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    const std::uint16_t triangleIndices[3] = {0, 1, 2};
    std::memcpy(fullscreenVertices->mappedData(), triangle, sizeof(triangle));
    std::memcpy(fullscreenIndices->mappedData(), triangleIndices, sizeof(triangleIndices));

    auto depthResult = device->createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
    auto targetResult = device->createOffscreenTarget({.extent = kExtent, .format = kColorFormat});
    auto maskResult = device->createOffscreenTarget({.extent = kExtent, .format = Format::Rgba8Unorm, .sampled = true});
    auto samplerResult = device->createSampler({});
    REQUIRE(depthResult.isOk());
    REQUIRE(targetResult.isOk());
    REQUIRE(maskResult.isOk());
    REQUIRE(samplerResult.isOk());
    depth = std::move(depthResult.value());
    target = std::move(targetResult.value());
    maskTarget = std::move(maskResult.value());
    sampler = std::move(samplerResult.value());
    readback = buffer(BufferPurpose::Readback, static_cast<std::size_t>(kExtent.width) * kExtent.height * 4);
  }

  ~Fixture() {
    target.reset();
    maskTarget.reset();
    static_cast<void>(device->waitIdle());
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;

  // One frame: the scene's meshes (writing depth) over black, then the overlay, then the readback.
  void render(const ViewportOverlayCamera& camera, const std::vector<const Mesh*>& sceneMeshes,
              const ViewportOverlayDrawList& list) {
    std::memcpy(sceneCamera->mappedData(), camera.view.data(), sizeof(camera.view));
    std::memcpy(static_cast<float*>(sceneCamera->mappedData()) + 16, camera.projection.data(), sizeof(camera.projection));
    auto acquired = target->acquireTarget();
    auto acquiredMask = maskTarget->acquireTarget();
    REQUIRE(acquired.isOk());
    REQUIRE(acquiredMask.isOk());
    std::unique_ptr<RenderTarget> color = std::move(acquired.value());
    std::unique_ptr<RenderTarget> maskColor = std::move(acquiredMask.value());
    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

    atlantis::render_graph::RenderGraphBuilder sceneGraph;
    const auto sceneColor = sceneGraph.declareResource("color");
    const auto sceneDepth = sceneGraph.declareResource("depth");
    const auto scenePass = sceneGraph.declarePass("scene");
    sceneGraph.writes(scenePass, sceneColor, ResourceState::ColorAttachmentOutput);
    sceneGraph.writes(scenePass, sceneDepth, ResourceState::DepthAttachmentReadWrite);
    sceneGraph.setExecute(scenePass, [&](CommandList& cmd) {
      for (const Mesh* mesh : sceneMeshes) {
        cmd.bindPipeline(*scene);
        cmd.bindVertexBuffer(mesh->vertexBuffer());
        cmd.bindIndexBuffer(mesh->indexBuffer());
        cmd.bindUniformBuffer(*sceneCamera);
        cmd.pushConstant(kIdentity.data(), sizeof(kIdentity));
        cmd.drawIndexed(mesh->indexCount());
      }
    });
    auto sceneCompiled = sceneGraph.compile();
    REQUIRE(sceneCompiled.isOk());
    // As drawFrame() leaves them when an overlay follows: the target in ColorAttachmentOutput.
    const auto sceneEnd = atlantis::render_graph::execute(
        sceneCompiled.value(),
        {{.resource = sceneCompiled.value().resourceAt(0),
          .target = color.get(),
          .colorClear = {0.0f, 0.0f, 0.0f, 1.0f},
          .finalState = ResourceState::ColorAttachmentOutput},
         {.resource = sceneCompiled.value().resourceAt(1), .depthTexture = depth.get(), .depthClear = 1.0f}},
        *commandList);

    const atlantis::renderer::ViewportOverlayResources resources{
        *tested, *onTop, *mask, *outline, *overlayCamera, *vertices, *indices, *fullscreenVertices, *fullscreenIndices,
        *maskColor, *sampler, false};
    const FrameEndStates end = atlantis::renderer::Renderer{}.drawViewportOverlay(
        *commandList, *color, *depth, FrameEndStates{.color = sceneEnd[0], .depth = sceneEnd[1]},
        ResourceState::TransferSource, camera, list, resources);
    REQUIRE(end.color == ResourceState::TransferSource);

    atlantis::render_graph::RenderGraphBuilder copy;
    const auto copyResource = copy.declareResource("color-copy");
    const auto copyPass = copy.declarePass("copy-to-buffer");
    copy.writes(copyPass, copyResource, ResourceState::TransferSource);
    copy.setExecute(copyPass, [&](CommandList& cmd) { cmd.copyRenderTargetToBuffer(*color, *readback); });
    auto copyCompiled = copy.compile();
    REQUIRE(copyCompiled.isOk());
    static_cast<void>(atlantis::render_graph::execute(
        copyCompiled.value(),
        {{.resource = copyCompiled.value().resourceAt(0), .target = color.get(), .incomingState = end.color}},
        *commandList));

    REQUIRE(device->submit(std::move(commandList), *color).isOk());
    REQUIRE(device->waitIdle().isOk());
    color.reset();
    maskColor.reset();
  }

  [[nodiscard]] Pixel at(std::uint32_t x, std::uint32_t y) const {
    const auto* data = static_cast<const std::uint8_t*>(readback->mappedData());
    const std::size_t offset = (static_cast<std::size_t>(y) * kExtent.width + x) * 4;
    return {data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
  }

  [[nodiscard]] int countInRow(std::uint32_t y, const Pixel& pixel) const {
    int count = 0;
    for (std::uint32_t x = 0; x < kExtent.width; ++x) count += at(x, y) == pixel ? 1 : 0;
    return count;
  }
};

[[nodiscard]] ViewportOverlayCamera identityCamera() { return ViewportOverlayCamera{kIdentity, kIdentity}; }

[[nodiscard]] bool isOutlineColor(const Pixel& p) {
  const auto close = [](std::uint8_t value, float expected) { return std::abs(value - expected * 255.0f) <= 1.0f; };
  const auto& c = atlantis::renderer::kSelectionOutlineColor;
  return close(p[0], c[0]) && close(p[1], c[1]) && close(p[2], c[2]);
}

}  // namespace

TEST_CASE("overlay primitives land at the pixels their positions project to", "[vulkan_backend][gpu][viewport_overlay]") {
  Fixture f;
  ViewportOverlayDrawList list;
  // A 4-pixel segment along NDC y = 0 from x = -0.5 to 0.5: pixels x in [14, 50) (2-pixel caps), rows 30-33.
  list.addSegment({-0.5f, 0.0f, 0.5f}, {0.5f, 0.0f, 0.5f}, kRed, ViewportOverlayDepth::OnTop, 4.0f);
  // A triangle in the lower-right corner, depth-tested against the cleared depth (1): drawn.
  list.addTriangle({0.2f, -0.9f, 0.5f}, {0.9f, -0.9f, 0.5f}, {0.9f, -0.2f, 0.5f}, kGreen, ViewportOverlayDepth::Tested);
  // A 6-pixel point at NDC (-0.5, -0.5): pixels (13..18, 13..18).
  list.addPoint({-0.5f, -0.5f, 0.5f}, kBlue, ViewportOverlayDepth::OnTop, 6.0f);
  f.render(identityCamera(), {}, list);

  CHECK(f.at(32, 32) == kRedPixel);
  CHECK(f.at(20, 31) == kRedPixel);
  CHECK(f.at(15, 32) == kRedPixel);   // inside the left cap
  CHECK(f.at(12, 32) == kBlack);      // past it
  CHECK(f.at(32, 35) == kBlack);      // beyond half the width
  CHECK(f.at(16, 16) == kBluePixel);
  CHECK(f.at(16, 21) == kBlack);
  CHECK(f.at(53, 10) == kGreenPixel);  // the triangle's centroid
  CHECK(f.at(30, 10) == kBlack);       // left of it
  CHECK(f.at(5, 60) == kBlack);
}

TEST_CASE("a depth-tested primitive behind a mesh is hidden; an on-top one is not",
          "[vulkan_backend][gpu][viewport_overlay]") {
  Fixture f;
  const Mesh occluder = makeQuad(*f.device, -1.0f, 0.0f, -1.0f, 1.0f, 0.3f, {0.5f, 0.5f, 0.5f});  // the left half
  ViewportOverlayDrawList list;
  list.addSegment({-0.8f, 0.4f, 0.6f}, {0.8f, 0.4f, 0.6f}, kGreen, ViewportOverlayDepth::Tested, 4.0f);  // rows ~43-46
  list.addSegment({-0.8f, -0.4f, 0.6f}, {0.8f, -0.4f, 0.6f}, kBlue, ViewportOverlayDepth::OnTop, 4.0f);  // rows ~17-20
  f.render(identityCamera(), {&occluder}, list);

  const Pixel gray = f.at(16, 60);
  CHECK(gray != kBlack);
  CHECK(f.at(16, 45) == gray);          // behind the occluder: hidden
  CHECK(f.at(48, 45) == kGreenPixel);   // in the open: drawn
  CHECK(f.at(16, 19) == kBluePixel);    // on top: drawn over the occluder
  CHECK(f.at(48, 19) == kBluePixel);
}

TEST_CASE("a selected mesh's outline lies just outside its silhouette, over occluders, and nowhere else",
          "[vulkan_backend][gpu][viewport_overlay]") {
  Fixture f;
  // The selected square covers pixels [24, 40) on both axes; an occluder in front covers x >= 32.
  const Mesh selected = makeQuad(*f.device, -0.25f, 0.25f, -0.25f, 0.25f, 0.5f, {0.5f, 0.5f, 0.5f});
  const Mesh occluder = makeQuad(*f.device, 0.0f, 1.0f, -1.0f, 1.0f, 0.3f, {0.2f, 0.2f, 0.8f});
  ViewportOverlayDrawList list;
  list.selection.push_back(DrawItem{.mesh = &selected, .material = nullptr, .objectToWorld = kIdentity});
  f.render(identityCamera(), {&selected, &occluder}, list);

  const Pixel gray = f.at(28, 32);
  const Pixel occluderPixel = f.at(56, 32);
  CHECK_FALSE(isOutlineColor(gray));   // inside the silhouette: none
  CHECK(isOutlineColor(f.at(23, 32)));  // one and two pixels outside the left edge
  CHECK(isOutlineColor(f.at(22, 32)));
  CHECK(f.at(21, 32) == kBlack);        // three: nothing
  CHECK(isOutlineColor(f.at(40, 32)));  // outside the right edge, over the occluder
  CHECK(isOutlineColor(f.at(41, 32)));
  CHECK(f.at(43, 32) == occluderPixel);
  CHECK(isOutlineColor(f.at(32, 23)));  // above and below
  CHECK(isOutlineColor(f.at(32, 40)));
  CHECK(f.at(36, 32) == occluderPixel);  // the occluded inside: no outline
  CHECK(f.at(5, 5) == kBlack);
}

TEST_CASE("a screen-constant group keeps its pixel size at two camera distances; a world group does not",
          "[vulkan_backend][gpu][viewport_overlay]") {
  Fixture f;
  const ViewportOverlayCamera camera{kIdentity, perspective(1.0471976f, 0.1f, 100.0f)};
  const auto lengths = [&](float distance) {
    ViewportOverlayDrawList list;
    // One unit along +x drawn as 20 pixels at the anchor, 2 pixels wide (1-pixel caps): 22 pixels on row 32.
    list.addSegment({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, kRed, ViewportOverlayDepth::OnTop, 2.0f,
                    ViewportOverlayAnchor{{0.0f, 0.0f, -distance}, 20.0f});
    // A world segment of fixed length 0.5, on row ~46 (its y scaled with the distance to stay in view).
    list.addSegment({0.0f, 0.25f * distance, -distance}, {0.5f, 0.25f * distance, -distance}, kGreen,
                    ViewportOverlayDepth::OnTop, 2.0f);
    f.render(camera, {}, list);
    int world = 0;
    for (std::uint32_t y = 0; y < kExtent.height; ++y) world = std::max(world, f.countInRow(y, kGreenPixel));
    return std::array<int, 2>{f.countInRow(32, kRedPixel), world};
  };
  const auto atTwo = lengths(2.0f);
  const auto atEight = lengths(8.0f);
  CHECK(atTwo[0] >= 21);
  CHECK(atTwo[0] <= 23);
  CHECK(std::abs(atEight[0] - atTwo[0]) <= 1);  // screen-constant: the same length at either distance (rounding)
  CHECK(atEight[1] * 2 < atTwo[1]);             // the world segment shrinks with the distance
}

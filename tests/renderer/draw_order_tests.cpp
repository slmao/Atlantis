// Plan 0042 Milestone 3 (Spec 0042 R7/R8, ADR-0090 Decision 2): the draw
// order, the Mesh bounds centre it sorts by, and drawFrame()'s use of both
// -- all GPU-independent. computeDrawOrder() is tested on plain data;
// drawFrame() against FakeCommandList, whose recorded push constants
// identify each draw (PBR payloads are 112 bytes with objectToWorld first,
// shadow-pass payloads 64 bytes, the output transform's 4).

#include <atlantis/assert.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/draw_order.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../../src/renderer/src/mesh_bounds.h"
#include "fake_command_list.h"

using atlantis::render_graph::test::FakeBuffer;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeHdrColorTarget;
using atlantis::render_graph::test::FakePipeline;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampler;
using atlantis::render_graph::test::FakeShadowMap;
using atlantis::render_graph::test::FakeTexture;
using atlantis::renderer::computeDrawOrder;
using atlantis::renderer::computeLocalBoundsCentre;
using atlantis::renderer::DrawItem;
using atlantis::renderer::DrawSortInput;
using atlantis::renderer::Material;
using atlantis::renderer::MaterialAlphaMode;
using atlantis::renderer::MaterialEnvironmentBinding;
using atlantis::renderer::MaterialPushConstantLayout;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;
using atlantis::rhi::VertexAttributeFormat;
using atlantis::rhi::VertexInputLayout;

namespace {

using Order = std::vector<std::uint32_t>;
constexpr std::optional<std::array<float, 3>> kCameraAtOrigin = std::array<float, 3>{0.0f, 0.0f, 0.0f};

[[nodiscard]] DrawSortInput opaqueAt(float x, float y, float z) { return {.blended = false, .worldSortPoint = {x, y, z}}; }
[[nodiscard]] DrawSortInput blendedAt(float x, float y, float z) { return {.blended = true, .worldSortPoint = {x, y, z}}; }

// Counts ATLANTIS_CHECK failures instead of aborting, for its scope.
class CountingFailureHandler {
 public:
  CountingFailureHandler()
      : previous_(atlantis::assertions::setFailureHandler(
            [this](const atlantis::AssertFailureInfo&) { ++failures; })) {}
  ~CountingFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }
  CountingFailureHandler(const CountingFailureHandler&) = delete;
  CountingFailureHandler& operator=(const CountingFailureHandler&) = delete;

  int failures = 0;

 private:
  atlantis::AssertFailureHandler previous_;
};

}  // namespace

// ---------------------------------------------------------------------------
// computeDrawOrder(): plain data in, indices out.
// ---------------------------------------------------------------------------

TEST_CASE("computeDrawOrder(): an all-opaque list keeps caller order, with or without a camera",
          "[renderer][draw_order][transparency]") {
  const std::vector<DrawSortInput> items{opaqueAt(0, 0, -50), opaqueAt(0, 0, -1), opaqueAt(0, 0, -20),
                                         opaqueAt(9, 9, 9)};
  CHECK(computeDrawOrder(items, kCameraAtOrigin) == Order{0, 1, 2, 3});
  CountingFailureHandler handler;
  CHECK(computeDrawOrder(items, std::nullopt) == Order{0, 1, 2, 3});
  CHECK(handler.failures == 0);  // the camera is never needed without a blended item
  CHECK(computeDrawOrder({}, std::nullopt).empty());
}

TEST_CASE("computeDrawOrder(): an all-blended list is farthest-first", "[renderer][draw_order][transparency]") {
  const std::vector<DrawSortInput> items{blendedAt(0, 0, -2), blendedAt(0, 0, -30), blendedAt(4, 0, 0),
                                         blendedAt(0, -10, 0)};
  // Squared distances 4, 900, 16, 100.
  CHECK(computeDrawOrder(items, kCameraAtOrigin) == Order{1, 3, 2, 0});
}

TEST_CASE("computeDrawOrder(): blended items follow every opaque item, which keep caller order",
          "[renderer][draw_order][transparency]") {
  const std::vector<DrawSortInput> items{blendedAt(0, 0, -3), opaqueAt(0, 0, -100), blendedAt(0, 0, -8),
                                         opaqueAt(0, 0, -1), blendedAt(0, 0, -5)};
  CHECK(computeDrawOrder(items, kCameraAtOrigin) == Order{1, 3, 2, 4, 0});
}

TEST_CASE("computeDrawOrder(): the key is the squared distance from the given camera, not from the origin",
          "[renderer][draw_order][transparency]") {
  const std::vector<DrawSortInput> items{blendedAt(0, 0, 0), blendedAt(0, 0, 10)};
  CHECK(computeDrawOrder(items, std::array<float, 3>{0, 0, -5}) == Order{1, 0});  // 225 vs 25
  CHECK(computeDrawOrder(items, std::array<float, 3>{0, 0, 15}) == Order{0, 1});  // 225 vs 25
}

TEST_CASE("computeDrawOrder(): equidistant blended items keep caller order (deterministic ties)",
          "[renderer][draw_order][transparency]") {
  // Four points on the same sphere around the camera, plus a farther and
  // a nearer one bracketing them.
  const std::vector<DrawSortInput> items{blendedAt(0, 0, -5), blendedAt(5, 0, 0), blendedAt(0, 0, -9),
                                         blendedAt(0, 5, 0),  blendedAt(-3, 0, -4), blendedAt(0, 0, -1)};
  const Order expected{2, 0, 1, 3, 4, 5};
  CHECK(computeDrawOrder(items, kCameraAtOrigin) == expected);
  // Reversing the tied items' caller order reverses them in the result too.
  const std::vector<DrawSortInput> reversedTies{blendedAt(-3, 0, -4), blendedAt(0, 5, 0), blendedAt(0, 0, -9),
                                                blendedAt(5, 0, 0),  blendedAt(0, 0, -5), blendedAt(0, 0, -1)};
  CHECK(computeDrawOrder(reversedTies, kCameraAtOrigin) == Order{2, 0, 1, 3, 4, 5});
  for (int run = 0; run < 3; ++run) CHECK(computeDrawOrder(items, kCameraAtOrigin) == expected);
}

TEST_CASE("computeDrawOrder(): a blended item with no camera position trips the check",
          "[renderer][draw_order][transparency]") {
  const std::vector<DrawSortInput> items{blendedAt(0, 0, -1), opaqueAt(0, 0, -2), blendedAt(0, 0, -9)};
  CountingFailureHandler handler;
  const Order order = computeDrawOrder(items, std::nullopt);
  CHECK(handler.failures == 1);
  // Handled without aborting: every index still appears exactly once.
  CHECK(order == Order{1, 0, 2});
}

// ---------------------------------------------------------------------------
// computeLocalBoundsCentre(): the createMesh() scan (Plan 0042 P4, Q2 A).
// ---------------------------------------------------------------------------

namespace {

struct TestVertex {
  float colour[3];
  float position[3];  // deliberately not at offset 0
  float uv[2];
};

[[nodiscard]] VertexInputLayout testVertexLayout() {
  return VertexInputLayout{.strideBytes = sizeof(TestVertex),
                           .attributes = {{.location = 1, .offsetBytes = 0, .format = VertexAttributeFormat::Float3},
                                          {.location = 0,
                                           .offsetBytes = static_cast<std::uint32_t>(offsetof(TestVertex, position)),
                                           .format = VertexAttributeFormat::Float3},
                                          {.location = 2,
                                           .offsetBytes = static_cast<std::uint32_t>(offsetof(TestVertex, uv)),
                                           .format = VertexAttributeFormat::Float2}}};
}

}  // namespace

TEST_CASE("computeLocalBoundsCentre(): the AABB centre of the location-0 positions, read at their offset",
          "[renderer][mesh][transparency]") {
  // Positions span x [-1, 3], y [2, 8], z [-7, -1]; the colour and UV
  // values are large decoys that must never be read as positions.
  const TestVertex vertices[4] = {{{100, 100, 100}, {-1, 2, -1}, {50, 50}},
                                  {{100, 100, 100}, {3, 5, -7}, {50, 50}},
                                  {{100, 100, 100}, {0, 8, -4}, {50, 50}},
                                  {{100, 100, 100}, {2, 4, -2}, {50, 50}}};
  CountingFailureHandler handler;
  const auto centre = computeLocalBoundsCentre(testVertexLayout(), vertices, sizeof(vertices));
  CHECK(handler.failures == 0);
  CHECK(centre == std::array<float, 3>{1.0f, 5.0f, -4.0f});

  const auto single = computeLocalBoundsCentre(testVertexLayout(), vertices, sizeof(TestVertex));
  CHECK(single == std::array<float, 3>{-1.0f, 2.0f, -1.0f});
  CHECK(computeLocalBoundsCentre(testVertexLayout(), vertices, 0) == std::array<float, 3>{0.0f, 0.0f, 0.0f});
  CHECK(handler.failures == 0);
}

TEST_CASE("computeLocalBoundsCentre(): a layout that cannot locate a Float3 position is a checked error",
          "[renderer][mesh][transparency]") {
  const TestVertex vertices[2] = {{{0, 0, 0}, {1, 1, 1}, {0, 0}}, {{0, 0, 0}, {3, 3, 3}, {0, 0}}};
  constexpr std::array<float, 3> kOrigin{0.0f, 0.0f, 0.0f};

  VertexInputLayout emptyLayout;  // stride 0, no attributes -- the pre-Q2-A test call sites
  VertexInputLayout noPosition = testVertexLayout();
  noPosition.attributes.erase(noPosition.attributes.begin() + 1);
  VertexInputLayout float2Position = testVertexLayout();
  float2Position.attributes[1].format = VertexAttributeFormat::Float2;
  VertexInputLayout positionPastStride = testVertexLayout();
  positionPastStride.attributes[1].offsetBytes = sizeof(TestVertex) - 8;

  for (const VertexInputLayout* layout : {&emptyLayout, &noPosition, &float2Position, &positionPastStride}) {
    CountingFailureHandler handler;
    CHECK(computeLocalBoundsCentre(*layout, vertices, sizeof(vertices)) == kOrigin);
    CHECK(handler.failures == 1);
  }

  CountingFailureHandler handler;
  CHECK(computeLocalBoundsCentre(testVertexLayout(), vertices, sizeof(vertices) - 4) == kOrigin);
  CHECK(handler.failures == 1);  // not a whole number of vertices
}

// ---------------------------------------------------------------------------
// drawFrame(): the order it issues draws in, and the shadow pass (R8).
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kPbrPayloadBytes = 112;
constexpr std::size_t kShadowPayloadBytes = 64;

[[nodiscard]] std::array<float, 16> translation(float x, float y, float z) {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
}

[[nodiscard]] Mesh fakeMesh(std::array<float, 3> localBoundsCentre = {0.0f, 0.0f, 0.0f}) {
  return Mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
              std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3, localBoundsCentre);
}

[[nodiscard]] Material fakePbrMaterial(MaterialAlphaMode alphaMode) {
  return Material(std::make_unique<FakePipeline>(), MaterialPushConstantLayout::PbrDirectLit, nullptr, nullptr,
                  {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 1.0f, MaterialEnvironmentBinding::None, nullptr, 0.0f, 0.0f,
                  {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, alphaMode);
}

// The x/z of objectToWorld's translation for every recorded push constant
// of the given size, in record order.
[[nodiscard]] std::vector<std::array<float, 2>> recordedTranslations(const FakeCommandList& commandList,
                                                                     std::size_t payloadBytes) {
  std::vector<std::array<float, 2>> result;
  for (const auto& data : commandList.pushConstantData) {
    if (data.size() != payloadBytes) continue;
    float objectToWorld[16];
    std::memcpy(objectToWorld, data.data(), sizeof(objectToWorld));
    result.push_back({objectToWorld[12], objectToWorld[14]});
  }
  return result;
}

struct FrameResources {
  FakeRenderTarget colorTarget{"color"};
  FakeTexture depthTarget{"depth"};
  FakeBuffer cameraBuffer{atlantis::rhi::BufferPurpose::Uniform, 0};
  FakeHdrColorTarget hdrColorTarget{"hdr"};
  FakeBuffer fullscreenVertexBuffer{atlantis::rhi::BufferPurpose::Vertex, 0};
  FakeBuffer fullscreenIndexBuffer{atlantis::rhi::BufferPurpose::Index, 0};
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler{"output_transform_sampler"};
  FakeShadowMap shadowMap{"shadow_map"};
  FakeSampler shadowMapSampler{"shadow_map_sampler"};
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer{atlantis::rhi::BufferPurpose::Uniform, 0};

  void draw(FakeCommandList& commandList, const std::vector<DrawItem>& drawItems,
            const std::vector<DrawItem>& shadowCasters,
            const std::optional<std::array<float, 3>>& cameraWorldPosition) {
    Renderer renderer;
    renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                       atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                       fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, 0.0f, nullptr, nullptr,
                       shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, shadowCasters,
                       cameraWorldPosition);
  }
};

}  // namespace

TEST_CASE("drawFrame(): opaque items in caller order, then blended items farthest-first",
          "[renderer][draw_order][transparency]") {
  const Mesh mesh = fakeMesh();
  const Material opaque = fakePbrMaterial(MaterialAlphaMode::Opaque);
  const Material mask = fakePbrMaterial(MaterialAlphaMode::Mask);
  const Material blend = fakePbrMaterial(MaterialAlphaMode::Blend);
  // x tags each item; z places it in front of a camera at the origin.
  const std::vector<DrawItem> drawItems{
      {.mesh = &mesh, .material = &blend, .objectToWorld = translation(1, 0, -2)},
      {.mesh = &mesh, .material = &opaque, .objectToWorld = translation(2, 0, -50)},
      {.mesh = &mesh, .material = &blend, .objectToWorld = translation(3, 0, -10)},
      {.mesh = &mesh, .material = &mask, .objectToWorld = translation(4, 0, -1)},
      {.mesh = &mesh, .material = &blend, .objectToWorld = translation(5, 0, -6)},
  };

  FakeCommandList commandList;
  FrameResources resources;
  resources.draw(commandList, drawItems, {}, kCameraAtOrigin);

  const auto draws = recordedTranslations(commandList, kPbrPayloadBytes);
  REQUIRE(draws.size() == 5);
  CHECK(draws[0][0] == 2.0f);  // opaque, caller order
  CHECK(draws[1][0] == 4.0f);  // mask is opaque-queue, caller order
  CHECK(draws[2][0] == 3.0f);  // blended, z -10
  CHECK(draws[3][0] == 5.0f);  // blended, z -6
  CHECK(draws[4][0] == 1.0f);  // blended, z -2
}

TEST_CASE("drawFrame(): an all-opaque list is issued in exactly caller order, with no camera position",
          "[renderer][draw_order][transparency]") {
  const Mesh mesh = fakeMesh();
  const Material opaque = fakePbrMaterial(MaterialAlphaMode::Opaque);
  const std::vector<DrawItem> drawItems{{.mesh = &mesh, .material = &opaque, .objectToWorld = translation(1, 0, -1)},
                                        {.mesh = &mesh, .material = &opaque, .objectToWorld = translation(2, 0, -9)},
                                        {.mesh = &mesh, .material = &opaque, .objectToWorld = translation(3, 0, -5)}};
  FakeCommandList commandList;
  FrameResources resources;
  CountingFailureHandler handler;
  resources.draw(commandList, drawItems, {}, std::nullopt);
  CHECK(handler.failures == 0);
  const auto draws = recordedTranslations(commandList, kPbrPayloadBytes);
  REQUIRE(draws.size() == 3);
  CHECK(draws[0][0] == 1.0f);
  CHECK(draws[1][0] == 2.0f);
  CHECK(draws[2][0] == 3.0f);
}

TEST_CASE("drawFrame(): blended items sort by the Mesh's bounds centre, not the node origin",
          "[renderer][draw_order][transparency]") {
  // Same objectToWorld; the geometry of `nearMesh` sits 5 units toward the
  // camera from its origin, that of `farMesh` 5 units away from it.
  const Mesh nearMesh = fakeMesh({0.0f, 0.0f, 5.0f});
  const Mesh farMesh = fakeMesh({0.0f, 0.0f, -5.0f});
  const Material blend = fakePbrMaterial(MaterialAlphaMode::Blend);
  std::array<float, 16> tagNear = translation(0, 0, -20);
  std::array<float, 16> tagFar = translation(0, 0, -20);
  tagNear[13] = 1.0f;  // y tags which item was drawn; it does not change the z sort
  tagFar[13] = 2.0f;
  const std::vector<DrawItem> drawItems{{.mesh = &nearMesh, .material = &blend, .objectToWorld = tagNear},
                                        {.mesh = &farMesh, .material = &blend, .objectToWorld = tagFar}};
  FakeCommandList commandList;
  FrameResources resources;
  resources.draw(commandList, drawItems, {}, kCameraAtOrigin);

  std::vector<float> drawnTags;
  for (const auto& data : commandList.pushConstantData) {
    if (data.size() != kPbrPayloadBytes) continue;
    float objectToWorld[16];
    std::memcpy(objectToWorld, data.data(), sizeof(objectToWorld));
    drawnTags.push_back(objectToWorld[13]);
  }
  CHECK(drawnTags == std::vector<float>{2.0f, 1.0f});  // far (sort z -25) before near (sort z -15)
}

TEST_CASE("drawFrame(): the shadow pass skips blended casters; opaque and mask casters still cast (R8)",
          "[renderer][draw_order][transparency][shadow]") {
  const Mesh mesh = fakeMesh();
  const Material opaque = fakePbrMaterial(MaterialAlphaMode::Opaque);
  const Material mask = fakePbrMaterial(MaterialAlphaMode::Mask);
  const Material blend = fakePbrMaterial(MaterialAlphaMode::Blend);
  const std::vector<DrawItem> drawItems{{.mesh = &mesh, .material = &blend, .objectToWorld = translation(1, 0, -3)},
                                        {.mesh = &mesh, .material = &opaque, .objectToWorld = translation(2, 0, -3)},
                                        {.mesh = &mesh, .material = &mask, .objectToWorld = translation(3, 0, -3)},
                                        {.mesh = &mesh, .material = &blend, .objectToWorld = translation(4, 0, -3)}};
  FakeCommandList commandList;
  FrameResources resources;
  resources.draw(commandList, drawItems, drawItems, kCameraAtOrigin);

  const auto shadowDraws = recordedTranslations(commandList, kShadowPayloadBytes);
  REQUIRE(shadowDraws.size() == 2);
  CHECK(shadowDraws[0][0] == 2.0f);
  CHECK(shadowDraws[1][0] == 3.0f);
  // Every item still draws in the main pass: 2 shadow + 4 main + 1 output transform.
  CHECK(recordedTranslations(commandList, kPbrPayloadBytes).size() == 4);
  CHECK(commandList.drawIndexedCounts.size() == 7);
}

TEST_CASE("drawFrame(): a blended draw item with no camera position trips the check",
          "[renderer][draw_order][transparency]") {
  const Mesh mesh = fakeMesh();
  const Material blend = fakePbrMaterial(MaterialAlphaMode::Blend);
  const std::vector<DrawItem> drawItems{{.mesh = &mesh, .material = &blend, .objectToWorld = translation(1, 0, -3)}};
  FakeCommandList commandList;
  FrameResources resources;
  CountingFailureHandler handler;
  resources.draw(commandList, drawItems, {}, std::nullopt);
  CHECK(handler.failures == 1);
}

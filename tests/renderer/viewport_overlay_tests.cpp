#include <atlantis/renderer/renderer.h>
#include <atlantis/renderer/viewport_overlay.h>
#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "fake_command_list.h"

// Plan 0066 M3b (ADR-0132 D3, ADR-0133 D5): the Viewport overlay's draw list
// and drawViewportOverlay()'s recording, against FakeCommandList -- the
// vertex layout, what each primitive appends, an empty list recording
// nothing, and the passes in order (mask, outline, tested, on top), each its
// own graph loading the target (and, for the tested one, the depth) from the
// end states the one before reported. Pixels are tested on the GPU
// (tests/vulkan_backend/viewport_overlay_gpu_tests.cpp).

namespace {

using atlantis::render_graph::test::FakeBuffer;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakePipeline;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampler;
using atlantis::render_graph::test::FakeTexture;
using atlantis::renderer::DrawItem;
using atlantis::renderer::FrameEndStates;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;
using atlantis::renderer::ViewportOverlayAnchor;
using atlantis::renderer::ViewportOverlayCamera;
using atlantis::renderer::ViewportOverlayDepth;
using atlantis::renderer::ViewportOverlayDrawList;
using atlantis::renderer::ViewportOverlayResources;
using atlantis::renderer::ViewportOverlayVertex;
using atlantis::rhi::AttachmentLoad;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::ResourceState;
using EventKind = FakeCommandList::EventKind;

constexpr std::array<float, 4> kRed{1.0f, 0.0f, 0.0f, 1.0f};

// The host's resources, all fakes.
struct Resources {
  FakePipeline tested;
  FakePipeline onTop;
  FakePipeline mask;
  FakePipeline outline;
  FakeBuffer camera{BufferPurpose::Uniform, atlantis::renderer::kViewportOverlayCameraSizeBytes};
  FakeBuffer vertices{BufferPurpose::Vertex, 64 * sizeof(ViewportOverlayVertex)};
  FakeBuffer indices{BufferPurpose::Index, 64 * sizeof(std::uint32_t), atlantis::rhi::IndexType::Uint32};
  FakeBuffer fullscreenVertices{BufferPurpose::Vertex, 24};
  FakeBuffer fullscreenIndices{BufferPurpose::Index, 6};
  FakeRenderTarget maskTarget{"mask"};
  FakeSampler maskSampler{"mask_sampler"};

  [[nodiscard]] ViewportOverlayResources view() {
    return ViewportOverlayResources{tested,     onTop, mask, outline, camera, vertices, indices, fullscreenVertices,
                                    fullscreenIndices, maskTarget, maskSampler, false};
  }
};

[[nodiscard]] Mesh fakeMesh() {
  return Mesh(std::make_unique<FakeBuffer>(BufferPurpose::Vertex, 0), std::make_unique<FakeBuffer>(BufferPurpose::Index, 0),
              6);
}

constexpr FrameEndStates kAfterDrawFrame{.color = ResourceState::ColorAttachmentOutput,
                                         .depth = ResourceState::DepthAttachmentReadWrite};

}  // namespace

TEST_CASE("the overlay's vertex layout matches ViewportOverlayVertex", "[renderer][viewport_overlay]") {
  const auto layout = atlantis::renderer::viewportOverlayVertexInputLayout();
  CHECK(layout.strideBytes == sizeof(ViewportOverlayVertex));
  REQUIRE(layout.attributes.size() == 5);
  for (std::uint32_t i = 0; i < 5; ++i) CHECK(layout.attributes[i].location == i);
  CHECK(layout.attributes[1].offsetBytes == offsetof(ViewportOverlayVertex, other));
  CHECK(layout.attributes[4].offsetBytes == offsetof(ViewportOverlayVertex, anchor));
  CHECK(layout.attributes[4].format == atlantis::rhi::VertexAttributeFormat::Float4);
}

TEST_CASE("each primitive appends its vertices and indices to its depth mode's list", "[renderer][viewport_overlay]") {
  ViewportOverlayDrawList list;
  CHECK(list.empty());
  list.addSegment({0, 0, 0}, {1, 0, 0}, kRed, ViewportOverlayDepth::OnTop, 3.0f, ViewportOverlayAnchor{{1, 2, 3}, 40.0f});
  list.addTriangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, kRed, ViewportOverlayDepth::Tested);
  list.addPoint({0, 0, 1}, kRed, ViewportOverlayDepth::OnTop, 6.0f);
  CHECK_FALSE(list.empty());

  REQUIRE(list.onTop.vertices.size() == 8);  // a segment's quad and a point's square
  CHECK(list.onTop.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7});
  const ViewportOverlayVertex& segmentEnd = list.onTop.vertices[3];
  CHECK(segmentEnd.position == std::array<float, 3>{0, 0, 0});
  CHECK(segmentEnd.other == std::array<float, 3>{1, 0, 0});
  CHECK(segmentEnd.shape == std::array<float, 4>{1.0f, 1.0f, 1.0f, 3.0f});  // segment, end b, side +1, 3 px
  CHECK(segmentEnd.anchor == std::array<float, 4>{1, 2, 3, 40.0f});
  CHECK(list.onTop.vertices[4].shape == std::array<float, 4>{2.0f, -1.0f, -1.0f, 6.0f});  // point, corner (-1, -1)
  CHECK(list.onTop.vertices[4].anchor[3] == 0.0f);                                          // a world group

  REQUIRE(list.tested.vertices.size() == 3);
  CHECK(list.tested.indices == std::vector<std::uint32_t>{0, 1, 2});
  CHECK(list.tested.vertices[2].shape[0] == 0.0f);
}

TEST_CASE("an empty overlay records nothing and returns the incoming end states", "[renderer][viewport_overlay]") {
  FakeCommandList commandList;
  FakeRenderTarget target{"target"};
  FakeTexture depth{"depth"};
  Resources resources;
  const FrameEndStates end = Renderer{}.drawViewportOverlay(commandList, target, depth, kAfterDrawFrame,
                                                            ResourceState::PresentSource, ViewportOverlayCamera{},
                                                            ViewportOverlayDrawList{}, resources.view());
  CHECK(commandList.events.empty());
  CHECK(end.color == ResourceState::ColorAttachmentOutput);
  CHECK(end.depth == ResourceState::DepthAttachmentReadWrite);
}

TEST_CASE("the overlay's passes: mask, outline, tested, on top -- each a graph continuing the last",
          "[renderer][viewport_overlay]") {
  const Mesh mesh = fakeMesh();
  ViewportOverlayDrawList list;
  list.selection.push_back(DrawItem{.mesh = &mesh, .material = nullptr, .objectToWorld = {}});
  list.addTriangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, kRed, ViewportOverlayDepth::Tested);
  list.addSegment({0, 0, 0}, {1, 0, 0}, kRed, ViewportOverlayDepth::OnTop, 2.0f);
  ViewportOverlayCamera camera;
  camera.view[0] = 7.0f;
  camera.projection[5] = 9.0f;

  FakeCommandList commandList;
  FakeRenderTarget target{"target"};
  FakeTexture depth{"depth"};
  Resources resources;
  const FrameEndStates end = Renderer{}.drawViewportOverlay(
      commandList, target, depth, kAfterDrawFrame, ResourceState::PresentSource, camera, list, resources.view());

  REQUIRE(commandList.beginRenderingCalls.size() == 4);
  const auto& scopes = commandList.beginRenderingCalls;
  CHECK(scopes[0].color == &resources.maskTarget);  // the mask, cleared
  CHECK(scopes[0].colorLoad == AttachmentLoad::Clear);
  CHECK(scopes[1].color == &target);  // the outline, over the frame
  CHECK(scopes[1].colorLoad == AttachmentLoad::Load);
  CHECK(scopes[1].depth == nullptr);
  CHECK(scopes[2].color == &target);  // tested: against the frame's depth
  CHECK(scopes[2].depth == &depth);
  CHECK(scopes[2].colorLoad == AttachmentLoad::Load);
  CHECK(scopes[2].depthLoad == AttachmentLoad::Load);
  CHECK(scopes[3].color == &target);  // on top: no depth
  CHECK(scopes[3].depth == nullptr);
  CHECK(scopes[3].colorLoad == AttachmentLoad::Load);

  // The target: a same-state barrier before each of its three passes, then its final state.
  std::vector<std::pair<ResourceState, ResourceState>> targetTransitions;
  for (const auto& t : commandList.transitions) {
    if (t.target == &target) targetTransitions.emplace_back(t.before, t.after);
  }
  const auto same = std::pair{ResourceState::ColorAttachmentOutput, ResourceState::ColorAttachmentOutput};
  CHECK(targetTransitions == std::vector<std::pair<ResourceState, ResourceState>>{
                                 same, same, same, {ResourceState::ColorAttachmentOutput, ResourceState::PresentSource}});
  // The mask: cleared from Undefined, left ShaderRead for the outline to sample.
  std::vector<std::pair<ResourceState, ResourceState>> maskTransitions;
  for (const auto& t : commandList.transitions) {
    if (t.target == &resources.maskTarget) maskTransitions.emplace_back(t.before, t.after);
  }
  CHECK(maskTransitions == std::vector<std::pair<ResourceState, ResourceState>>{
                               {ResourceState::Undefined, ResourceState::ColorAttachmentOutput},
                               {ResourceState::ColorAttachmentOutput, ResourceState::ShaderRead}});
  // The depth: one same-state barrier, before the tested pass loads it.
  REQUIRE(commandList.textureTransitions.size() == 1);
  CHECK(commandList.textureTransitions[0].before == ResourceState::DepthAttachmentReadWrite);
  CHECK(commandList.textureTransitions[0].after == ResourceState::DepthAttachmentReadWrite);

  CHECK(end.color == ResourceState::PresentSource);
  CHECK(end.depth == ResourceState::DepthAttachmentReadWrite);

  // The camera and the primitives were written: the tested vertices first, then the on-top ones.
  const auto* cameraData = static_cast<const float*>(resources.camera.mappedData());
  CHECK(cameraData[0] == 7.0f);
  CHECK(cameraData[16 + 5] == 9.0f);
  const auto* vertices = static_cast<const ViewportOverlayVertex*>(resources.vertices.mappedData());
  CHECK(vertices[0].shape[0] == 0.0f);  // the triangle
  CHECK(vertices[3].shape[0] == 1.0f);  // the segment, after it
  const auto* indices = static_cast<const std::uint32_t*>(resources.indices.mappedData());
  CHECK(indices[3] == 0);  // the on-top indices are their own list's, drawn at a vertex offset
}

TEST_CASE("an overlay of on-top primitives only is one pass, ending in the frame's final state",
          "[renderer][viewport_overlay]") {
  ViewportOverlayDrawList list;
  list.addPoint({0, 0, 0}, kRed, ViewportOverlayDepth::OnTop, 4.0f);
  FakeCommandList commandList;
  FakeRenderTarget target{"target"};
  FakeTexture depth{"depth"};
  Resources resources;
  const FrameEndStates end = Renderer{}.drawViewportOverlay(commandList, target, depth, kAfterDrawFrame,
                                                            ResourceState::ShaderRead, ViewportOverlayCamera{}, list,
                                                            resources.view());
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.textureTransitions.empty());  // the depth is not touched
  REQUIRE(commandList.transitions.size() == 2);
  CHECK(commandList.transitions[1].after == ResourceState::ShaderRead);
  CHECK(end.color == ResourceState::ShaderRead);
  CHECK(end.depth == ResourceState::DepthAttachmentReadWrite);
}

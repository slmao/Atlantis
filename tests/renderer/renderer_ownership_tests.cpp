#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>

#include <array>
#include <memory>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fake_command_list.h"

// Exercises Plan 0007 Section 11/15: Mesh/Material are movable-not-
// copyable (ADR-0022's caller-owned-aggregate ownership model); Renderer
// itself has no member state, so it keeps every special member at its
// trivial compiler-generated default -- copyable and movable, unlike
// RenderGraphBuilder, per Section 11's own explicit correction of an
// earlier revision that unnecessarily deleted Renderer's copy/move
// (Human Review Approval item 4); and drawFrame() against a fake
// CommandList records the expected bind/draw sequence per DrawItem, with
// distinct per-item push-constant data -- the concrete regression test
// for the push-constant-vs-shared-uniform-buffer correctness argument
// (ADR-0025). No Vulkan device anywhere in this test binary.

namespace {

using atlantis::render_graph::test::FakeBuffer;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeHdrColorTarget;
using atlantis::render_graph::test::FakePipeline;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampledTexture;
using atlantis::render_graph::test::FakeSampler;
using atlantis::render_graph::test::FakeTexture;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;

}  // namespace

TEST_CASE("Material's sampledTexture()/sampler() are non-owning raw pointer types (V22)", "[renderer][ownership]") {
  STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const Material&>().sampledTexture()),
                                 const atlantis::rhi::SampledTexture*>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<const Material&>().sampler()), const atlantis::rhi::Sampler*>);
}

TEST_CASE("Mesh is movable, not copyable", "[renderer][ownership]") {
  STATIC_REQUIRE(std::is_move_constructible_v<Mesh>);
  STATIC_REQUIRE(std::is_move_assignable_v<Mesh>);
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Mesh>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Mesh>);
}

TEST_CASE("Material is movable, not copyable", "[renderer][ownership]") {
  STATIC_REQUIRE(std::is_move_constructible_v<Material>);
  STATIC_REQUIRE(std::is_move_assignable_v<Material>);
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Material>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Material>);
}

TEST_CASE("Renderer keeps every special member at its trivial default -- copyable and movable, no member state",
          "[renderer][ownership]") {
  STATIC_REQUIRE(std::is_copy_constructible_v<Renderer>);
  STATIC_REQUIRE(std::is_copy_assignable_v<Renderer>);
  STATIC_REQUIRE(std::is_move_constructible_v<Renderer>);
  STATIC_REQUIRE(std::is_move_assignable_v<Renderer>);
  STATIC_REQUIRE(std::is_empty_v<Renderer>);
}

TEST_CASE("Renderer::drawFrame() records a full bind/draw sequence per DrawItem with distinct push-constant data",
          "[renderer][ownership]") {
  // No real GPU device anywhere -- FakeBuffer/FakePipeline back Mesh/
  // Material with no real Vulkan resource; FakeCommandList's own
  // bind*()/pushConstant()/drawIndexed() overrides only ever record a
  // call's pointer/byte arguments, never invoke a method on the bound
  // object itself. This exercises reference reuse (both DrawItems share
  // the same Mesh/Material instance), not a cache -- Renderer itself
  // creates neither.
  atlantis::renderer::Mesh sharedMesh(
      std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
      std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  atlantis::renderer::Material sharedMaterial(std::make_unique<FakePipeline>(),
                                               atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);

  DrawItem itemA;
  itemA.mesh = &sharedMesh;
  itemA.material = &sharedMaterial;
  itemA.objectToWorld = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f, 5.0f, 0.0f, 0.0f, 1.0f};

  DrawItem itemB;
  itemB.mesh = &sharedMesh;
  itemB.material = &sharedMaterial;
  itemB.objectToWorld = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f, -5.0f, 0.0f, 0.0f, 1.0f};

  const std::vector<DrawItem> drawItems{itemA, itemB};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  FakeCommandList commandList;

  // cameraUniformBuffer is only ever passed to bindUniformBuffer(), which
  // FakeCommandList only records the pointer identity of -- a real
  // FakeBuffer instance all the same, no null dereference anywhere in
  // this test.
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  // Plan 0024 Milestone 5: the output-transform pass's own five new,
  // caller-owned resources -- Renderer only ever borrows them.
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output_transform_sampler");

  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler);

  // Plan 0024 Milestone 5: one more bindPipeline/bindVertexBuffer/
  // bindIndexBuffer/drawIndexed than before -- the output-transform
  // pass's own fullscreen-triangle draw, in addition to the two
  // DrawItems' own geometry-pass draws. bindUniformBuffer/pushConstant
  // counts are unchanged -- the output-transform pass calls neither.
  REQUIRE(commandList.boundPipelines.size() == 3);
  REQUIRE(commandList.boundVertexBuffers.size() == 3);
  REQUIRE(commandList.boundIndexBuffers.size() == 3);
  REQUIRE(commandList.boundUniformBuffers.size() == 2);
  REQUIRE(commandList.pushConstants.size() == 2);
  REQUIRE(commandList.drawIndexedCounts.size() == 3);
  REQUIRE(commandList.drawIndexedCounts[0] == 3);
  REQUIRE(commandList.drawIndexedCounts[1] == 3);
  REQUIRE(commandList.drawIndexedCounts[2] == 3);  // the fullscreen triangle's own 3 indices

  // Distinct push-constant bytes per DrawItem -- the two DrawItems' own
  // objectToWorld values must not have collided.
  REQUIRE(commandList.pushConstantData[0] != commandList.pushConstantData[1]);

  const auto* firstAsFloats = reinterpret_cast<const float*>(commandList.pushConstantData[0].data());
  const auto* secondAsFloats = reinterpret_cast<const float*>(commandList.pushConstantData[1].data());
  REQUIRE(firstAsFloats[12] == 5.0f);
  REQUIRE(secondAsFloats[12] == -5.0f);

  // Plan 0024 Milestone 5: the geometry pass now writes hdrColorTarget
  // (the new beginRendering(HdrColorTarget&, ...) overload, with the
  // depth attachment) -- the output-transform pass writes colorTarget
  // (the existing beginRendering(RenderTarget&, ...) overload, with no
  // depth attachment, since it declares no DepthAttachmentReadWrite
  // usage).
  REQUIRE(commandList.beginRenderingHdrCalls.size() == 1);
  REQUIRE(commandList.beginRenderingHdrCalls[0].color == &hdrColorTarget);
  REQUIRE(commandList.beginRenderingHdrCalls[0].depth == &depthTarget);

  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  REQUIRE(commandList.beginRenderingCalls[0].color == &colorTarget);
  REQUIRE(commandList.beginRenderingCalls[0].depth == nullptr);

  // The output-transform pass's own bindTexture(HdrColorTarget&, ...)
  // call -- a distinct overload/event from the SampledTexture one
  // (BindHdrTexture, not BindTexture) -- see the two Material-textured/
  // untextured test cases below for that overload's own coverage.
  REQUIRE(commandList.boundHdrTextures.size() == 1);
  REQUIRE(commandList.boundHdrTextures[0].texture == &hdrColorTarget);
  REQUIRE(commandList.boundHdrTextures[0].sampler == &outputTransformSampler);
}

TEST_CASE("Renderer::drawFrame() passes finalColorState through unmodified, never branching on it",
          "[renderer][final_color_state]") {
  // Spec 0010/ADR-0022 Accepted Amendment: a windowed caller supplies
  // PresentSource, a headless caller supplies TransferSource directly --
  // Renderer itself must not observe, validate, or branch on the value.
  // Confirmed here by running the exact same draw twice, against two
  // fresh FakeCommandList instances, and diffing their recorded event
  // sequences: every event must match except the final transitions
  // entry's `after` field, which must equal each call's own supplied
  // finalColorState.
  atlantis::renderer::Mesh sharedMesh(
      std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
      std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  atlantis::renderer::Material sharedMaterial(std::make_unique<FakePipeline>(),
                                               atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);

  DrawItem item;
  item.mesh = &sharedMesh;
  item.material = &sharedMaterial;
  item.objectToWorld = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  const std::vector<DrawItem> drawItems{item};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);
  Renderer renderer;

  // Plan 0024 Milestone 5: the output-transform pass's own five new,
  // caller-owned resources, shared by both calls below -- Renderer only
  // ever borrows them.
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output_transform_sampler");

  FakeCommandList windowedCommandList;
  renderer.drawFrame(windowedCommandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler);

  FakeCommandList headlessCommandList;
  renderer.drawFrame(headlessCommandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::TransferSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler);

  REQUIRE(windowedCommandList.events == headlessCommandList.events);
  REQUIRE(windowedCommandList.transitions.size() == headlessCommandList.transitions.size());
  REQUIRE(windowedCommandList.transitions.size() >= 1);

  const std::size_t lastIndex = windowedCommandList.transitions.size() - 1;
  for (std::size_t i = 0; i < lastIndex; ++i) {
    REQUIRE(windowedCommandList.transitions[i].target == headlessCommandList.transitions[i].target);
    REQUIRE(windowedCommandList.transitions[i].before == headlessCommandList.transitions[i].before);
    REQUIRE(windowedCommandList.transitions[i].after == headlessCommandList.transitions[i].after);
  }

  REQUIRE(windowedCommandList.transitions[lastIndex].target == headlessCommandList.transitions[lastIndex].target);
  REQUIRE(windowedCommandList.transitions[lastIndex].before == headlessCommandList.transitions[lastIndex].before);
  REQUIRE(windowedCommandList.transitions[lastIndex].after == atlantis::rhi::ResourceState::PresentSource);
  REQUIRE(headlessCommandList.transitions[lastIndex].after == atlantis::rhi::ResourceState::TransferSource);
}

TEST_CASE("Renderer::drawFrame() with an untextured Material records no bindTexture call (V23)",
          "[renderer][ownership][sampled_texture]") {
  atlantis::renderer::Mesh mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
                                 std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  atlantis::renderer::Material material(
      std::make_unique<FakePipeline>(),
      atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);  // untextured -- sampledTexture() == nullptr
  REQUIRE(material.sampledTexture() == nullptr);
  REQUIRE(material.sampler() == nullptr);

  DrawItem item;
  item.mesh = &mesh;
  item.material = &material;
  item.objectToWorld = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  const std::vector<DrawItem> drawItems{item};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);
  FakeCommandList commandList;
  Renderer renderer;

  // Plan 0024 Milestone 5: the output-transform pass's own five new,
  // caller-owned resources -- Renderer only ever borrows them.
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output_transform_sampler");

  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler);

  // The Material's own untextured DrawItem records no SampledTexture-
  // shaped bindTexture() call -- the output-transform pass's own
  // bindTexture(HdrColorTarget&, ...) call is a distinct overload/event
  // (BindHdrTexture, not BindTexture), so it does not interfere with
  // either check below.
  REQUIRE(commandList.boundTextures.empty());
  for (const FakeCommandList::EventKind event : commandList.events) {
    REQUIRE(event != FakeCommandList::EventKind::BindTexture);
  }
}

TEST_CASE("Renderer::drawFrame() with a textured Material records bindTexture immediately after "
          "bindUniformBuffer (V24)",
          "[renderer][ownership][sampled_texture]") {
  atlantis::renderer::Mesh mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
                                 std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  FakeSampledTexture fakeTexture("texture");
  FakeSampler fakeSampler("sampler");
  atlantis::renderer::Material material(std::make_unique<FakePipeline>(),
                                         atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly,
                                         &fakeTexture, &fakeSampler);
  REQUIRE(material.sampledTexture() == &fakeTexture);
  REQUIRE(material.sampler() == &fakeSampler);

  DrawItem itemA;
  itemA.mesh = &mesh;
  itemA.material = &material;
  itemA.objectToWorld = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  DrawItem itemB = itemA;
  const std::vector<DrawItem> drawItems{itemA, itemB};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);
  FakeCommandList commandList;
  Renderer renderer;

  // Plan 0024 Milestone 5: the output-transform pass's own five new,
  // caller-owned resources -- Renderer only ever borrows them.
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output_transform_sampler");

  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler);

  // boundTextures counts only the SampledTexture-shaped bindTexture()
  // overload -- the output-transform pass's own bindTexture(HdrColorTarget&,
  // ...) call is recorded separately (boundHdrTextures), so this count
  // is unaffected.
  REQUIRE(commandList.boundTextures.size() == 2);
  REQUIRE(commandList.boundTextures[0].texture == &fakeTexture);
  REQUIRE(commandList.boundTextures[0].sampler == &fakeSampler);
  REQUIRE(commandList.boundTextures[1].texture == &fakeTexture);
  REQUIRE(commandList.boundTextures[1].sampler == &fakeSampler);

  // Positioned immediately after bindUniformBuffer, for each DrawItem.
  const auto& events = commandList.events;
  int foundPairs = 0;
  for (std::size_t i = 0; i + 1 < events.size(); ++i) {
    if (events[i] == FakeCommandList::EventKind::BindUniformBuffer &&
        events[i + 1] == FakeCommandList::EventKind::BindTexture) {
      ++foundPairs;
    }
  }
  REQUIRE(foundPairs == 2);
}

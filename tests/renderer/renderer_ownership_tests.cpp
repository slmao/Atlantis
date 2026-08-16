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
using atlantis::render_graph::test::FakePipeline;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeTexture;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;

}  // namespace

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
  atlantis::renderer::Material sharedMaterial(std::make_unique<FakePipeline>());

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

  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource);

  REQUIRE(commandList.boundPipelines.size() == 2);
  REQUIRE(commandList.boundVertexBuffers.size() == 2);
  REQUIRE(commandList.boundIndexBuffers.size() == 2);
  REQUIRE(commandList.boundUniformBuffers.size() == 2);
  REQUIRE(commandList.pushConstants.size() == 2);
  REQUIRE(commandList.drawIndexedCounts.size() == 2);
  REQUIRE(commandList.drawIndexedCounts[0] == 3);
  REQUIRE(commandList.drawIndexedCounts[1] == 3);

  // Distinct push-constant bytes per DrawItem -- the two DrawItems' own
  // objectToWorld values must not have collided.
  REQUIRE(commandList.pushConstantData[0] != commandList.pushConstantData[1]);

  const auto* firstAsFloats = reinterpret_cast<const float*>(commandList.pushConstantData[0].data());
  const auto* secondAsFloats = reinterpret_cast<const float*>(commandList.pushConstantData[1].data());
  REQUIRE(firstAsFloats[12] == 5.0f);
  REQUIRE(secondAsFloats[12] == -5.0f);

  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  REQUIRE(commandList.beginRenderingCalls[0].color == &colorTarget);
  REQUIRE(commandList.beginRenderingCalls[0].depth == &depthTarget);
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
  atlantis::renderer::Material sharedMaterial(std::make_unique<FakePipeline>());

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

  FakeCommandList windowedCommandList;
  renderer.drawFrame(windowedCommandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource);

  FakeCommandList headlessCommandList;
  renderer.drawFrame(headlessCommandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::TransferSource);

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

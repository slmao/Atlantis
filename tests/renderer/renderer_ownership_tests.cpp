#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/assert.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
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
using atlantis::render_graph::test::FakeShadowMap;
using atlantis::render_graph::test::FakeTexture;
using atlantis::renderer::DrawItem;
using atlantis::renderer::EnvironmentLighting;
using atlantis::renderer::Material;
using atlantis::renderer::MaterialEnvironmentBinding;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;

class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<std::string>& failures)
      : previous_(atlantis::assertions::setFailureHandler(
            [&failures](const atlantis::AssertFailureInfo& info) { failures.emplace_back(info.message); })) {}
  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

}  // namespace

TEST_CASE("Material's sampledTexture()/sampler() are non-owning raw pointer types (V22)", "[renderer][ownership]") {
  STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const Material&>().sampledTexture()),
                                 const atlantis::rhi::SampledTexture*>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<const Material&>().sampler()), const atlantis::rhi::Sampler*>);
  Material material(std::make_unique<FakePipeline>(),
                    atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);
  CHECK(material.environmentBinding() == MaterialEnvironmentBinding::None);
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

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal FakeShadowMap/
  // FakeSampler/FakePipeline/FakeBuffer set -- shadowCasterDrawItems
  // stays empty; neither DrawItem here is PbrDirectLit-kind, so the
  // shadow-map bind is never actually exercised in this TEST_CASE.
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, nullptr, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

  // Plan 0024 Milestone 5: one more bindPipeline/bindVertexBuffer/
  // bindIndexBuffer/drawIndexed than before -- the output-transform
  // pass's own fullscreen-triangle draw, in addition to the two
  // DrawItems' own geometry-pass draws. bindUniformBuffer/pushConstant
  // counts are unchanged -- the output-transform pass calls neither.
  // Plan 0027 Milestone 9 (ADR-0072 D-1/P6): one more bindPipeline and
  // bindUniformBuffer than that -- the "shadow" pass's own unconditional
  // pair (outside its own empty-shadowCasterDrawItems loop), first every
  // frame. pushConstant/drawIndexed counts are unaffected -- the shadow
  // pass calls neither with an empty caster list.
  REQUIRE(commandList.boundPipelines.size() == 4);
  REQUIRE(commandList.boundVertexBuffers.size() == 3);
  REQUIRE(commandList.boundIndexBuffers.size() == 3);
  REQUIRE(commandList.boundUniformBuffers.size() == 3);
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
  REQUIRE(commandList.boundHdrTextures[0].binding == 0);
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

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): shared by both calls below,
  // mirroring the output-transform resources' own "shared by both calls"
  // shape immediately above.
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  FakeCommandList windowedCommandList;
  renderer.drawFrame(windowedCommandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, nullptr, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

  FakeCommandList headlessCommandList;
  renderer.drawFrame(headlessCommandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::TransferSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, nullptr, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

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

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal FakeShadowMap/
  // FakeSampler/FakePipeline/FakeBuffer set -- shadowCasterDrawItems
  // stays empty; the DrawItem here is not PbrDirectLit-kind, so the
  // shadow-map bind is never actually exercised.
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, nullptr, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

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

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal FakeShadowMap/
  // FakeSampler/FakePipeline/FakeBuffer set -- shadowCasterDrawItems
  // stays empty; neither DrawItem here is PbrDirectLit-kind, so the
  // shadow-map bind is never actually exercised.
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, nullptr, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

  // boundTextures counts only the SampledTexture-shaped bindTexture()
  // overload -- the output-transform pass's own bindTexture(HdrColorTarget&,
  // ...) call is recorded separately (boundHdrTextures), so this count
  // is unaffected.
  REQUIRE(commandList.boundTextures.size() == 2);
  REQUIRE(commandList.boundTextures[0].binding == 1);
  REQUIRE(commandList.boundTextures[0].texture == &fakeTexture);
  REQUIRE(commandList.boundTextures[0].sampler == &fakeSampler);
  REQUIRE(commandList.boundTextures[1].texture == &fakeTexture);
  REQUIRE(commandList.boundTextures[1].binding == 1);
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

TEST_CASE("Renderer binds base color, environment cube, and DFG LUT at explicit slots for an IBL Material",
          "[renderer][ownership][pbr_ibl]") {
  Mesh mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
            std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  FakeSampledTexture baseColor("base-color");
  FakeSampledTexture environment("environment");
  FakeSampledTexture dfgLut("dfg-lut");
  FakeSampler baseColorSampler("base-color-sampler");
  FakeSampler environmentSampler("environment-sampler");
  FakeSampler dfgSampler("dfg-sampler");
  Material material(std::make_unique<FakePipeline>(),
                    atlantis::renderer::MaterialPushConstantLayout::PbrDirectLit, &baseColor,
                    &baseColorSampler, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F, 0.5F,
                    MaterialEnvironmentBinding::Ibl);
  CHECK(material.environmentBinding() == MaterialEnvironmentBinding::Ibl);

  DrawItem item{.mesh = &mesh,
                .material = &material,
                .objectToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 464 to 592 for
  // the new light-space view+projection tail (P5).
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 592);
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output-transform");
  const EnvironmentLighting lighting{environment, environmentSampler, dfgLut, dfgSampler};

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal FakeShadowMap/
  // FakeSampler/FakePipeline/FakeBuffer set -- shadowCasterDrawItems
  // stays empty. The shadow-map bind itself IS exercised here (the
  // DrawItem is PbrDirectLit+Ibl), landing in boundShadowMapTextures --
  // a separate vector from boundTextures below, so the existing
  // assertions on boundTextures are unaffected.
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  FakeCommandList commandList;
  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, std::span<const DrawItem>(&item, 1),
                     atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                     fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, &lighting, nullptr,
                     shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

  REQUIRE(commandList.boundTextures.size() == 3);
  CHECK(commandList.boundTextures[0].binding == 1);
  CHECK(commandList.boundTextures[0].texture == &baseColor);
  CHECK(commandList.boundTextures[1].binding == 2);
  CHECK(commandList.boundTextures[1].texture == &environment);
  CHECK(commandList.boundTextures[1].sampler == &environmentSampler);
  CHECK(commandList.boundTextures[2].binding == 3);
  CHECK(commandList.boundTextures[2].texture == &dfgLut);
  CHECK(commandList.boundTextures[2].sampler == &dfgSampler);

  std::vector<std::string> failures;
  ScopedFailureHandler failureHandler(failures);
  FakeCommandList missingLightingCommandList;
  renderer.drawFrame(missingLightingCommandList, colorTarget, depthTarget, cameraBuffer,
                     std::span<const DrawItem>(&item, 1), atlantis::rhi::ResourceState::PresentSource,
                     hdrColorTarget, fullscreenVertexBuffer, fullscreenIndexBuffer,
                     outputTransformPipeline, outputTransformSampler, nullptr, nullptr, shadowMap, shadowMapSampler,
                     shadowCastPipeline, shadowLightSpaceBuffer, {});
  REQUIRE(failures.size() == 1);
  CHECK(failures[0].find("IBL Material") != std::string::npos);
  CHECK(missingLightingCommandList.boundTextures.size() == 1);
}

TEST_CASE("Renderer draws the sky, bound at slot 1, strictly before every DrawItem when skyPipeline and "
          "environmentLighting are both non-null (Plan 0026 Milestone 2, ADR-0071)",
          "[renderer][ownership][sky]") {
  atlantis::renderer::Mesh mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
                                 std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  atlantis::renderer::Material material(std::make_unique<FakePipeline>(),
                                         atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);
  DrawItem item{.mesh = &mesh,
                .material = &material,
                .objectToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const std::vector<DrawItem> drawItems{item};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 464 to 592 for
  // the new light-space view+projection tail (P5).
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 592);
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output-transform");
  FakePipeline skyPipeline;
  FakeSampledTexture environment("environment");
  FakeSampledTexture dfgLut("dfg-lut");
  FakeSampler environmentSampler("environment-sampler");
  FakeSampler dfgSampler("dfg-sampler");
  const EnvironmentLighting lighting{environment, environmentSampler, dfgLut, dfgSampler};

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P6): the "shadow" pass always
  // binds shadowCastPipeline and calls bindUniformBuffer(), even with an
  // empty shadowCasterDrawItems -- both calls sit outside that loop
  // (P6's own execute-callback shape). This is a real, disclosed new
  // Pipeline bind, first every frame (the shadow pass has no color
  // output, so it never itself calls drawIndexed).
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  FakeCommandList commandList;
  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, &lighting,
                      &skyPipeline, shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

  // Four Pipelines bound in order: the shadow pass (always first --
  // P7's dependency-driven compile order), sky, the one DrawItem, then
  // the output-transform pass -- sky strictly before every DrawItem.
  REQUIRE(commandList.boundPipelines.size() == 4);
  CHECK(commandList.boundPipelines[0] == &shadowCastPipeline);
  CHECK(commandList.boundPipelines[1] == &skyPipeline);
  CHECK(commandList.drawIndexedCounts.size() == 3);  // the shadow pass itself never calls drawIndexed (empty caster list)
  CHECK(commandList.drawIndexedCounts[0] == 3);  // the sky's own fullscreen triangle

  // The sky's own cubemap bind -- the SampledTexture-shaped bindTexture()
  // overload, binding 1, distinct from the output-transform pass's own
  // BindHdrTexture event.
  REQUIRE(commandList.boundTextures.size() == 1);
  CHECK(commandList.boundTextures[0].binding == 1);
  CHECK(commandList.boundTextures[0].texture == &environment);
  CHECK(commandList.boundTextures[0].sampler == &environmentSampler);

  // The sky's own full bind/draw sequence (BindPipeline, BindVertexBuffer,
  // BindIndexBuffer, BindUniformBuffer, BindTexture, DrawIndexed) precedes
  // every event the one DrawItem's own draw records.
  const auto skyDrawEventIndex =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::DrawIndexed) -
      commandList.events.begin();
  const auto firstBindPipelineAfterSky =
      std::find(commandList.events.begin() + skyDrawEventIndex + 1, commandList.events.end(),
                FakeCommandList::EventKind::BindPipeline);
  REQUIRE(firstBindPipelineAfterSky != commandList.events.end());
  CHECK(commandList.boundPipelines[2] != &skyPipeline);
}

TEST_CASE("A non-null skyPipeline with a null environmentLighting fires the programmer-error guard and draws no "
          "sky (Plan 0026 Milestone 2, ADR-0071)",
          "[renderer][ownership][sky]") {
  atlantis::renderer::Mesh mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
                                 std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  atlantis::renderer::Material material(std::make_unique<FakePipeline>(),
                                         atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);
  DrawItem item{.mesh = &mesh,
                .material = &material,
                .objectToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const std::vector<DrawItem> drawItems{item};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 464 to 592 for
  // the new light-space view+projection tail (P5).
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 592);
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output-transform");
  FakePipeline skyPipeline;

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P6): the "shadow" pass always
  // binds shadowCastPipeline, regardless of skyPipeline/environmentLighting
  // -- unrelated guards.
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  std::vector<std::string> failures;
  ScopedFailureHandler failureHandler(failures);
  FakeCommandList commandList;
  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler,
                      /*environmentLighting=*/nullptr, &skyPipeline, shadowMap, shadowMapSampler, shadowCastPipeline,
                      shadowLightSpaceBuffer, {});

  REQUIRE(failures.size() == 1);
  CHECK(failures[0].find("skyPipeline") != std::string::npos);
  // The shadow pass's own Pipeline, the one DrawItem's own Pipeline, and
  // the output-transform Pipeline are bound -- the sky never binds or
  // draws.
  REQUIRE(commandList.boundPipelines.size() == 3);
  CHECK(commandList.boundPipelines[0] == &shadowCastPipeline);
  CHECK(commandList.boundPipelines[1] != &skyPipeline);
  CHECK(commandList.boundPipelines[2] != &skyPipeline);
}

TEST_CASE("Renderer::drawFrame() records the \"shadow\" pass's full draw sequence strictly before \"draw\"'s own "
          "sequence, for a non-empty shadowCasterDrawItems (Plan 0027 Milestone 9, ADR-0072 D-1/P6)",
          "[renderer][ownership][shadow]") {
  atlantis::renderer::Mesh casterMesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
                                       std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  atlantis::renderer::Material material(std::make_unique<FakePipeline>(),
                                         atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly);
  DrawItem casterItem{.mesh = &casterMesh,
                       .material = &material,
                       .objectToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const std::vector<DrawItem> shadowCasterDrawItems{casterItem};
  const std::vector<DrawItem> drawItems{casterItem};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 592);
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output-transform");
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  FakeCommandList commandList;
  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, nullptr, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer,
                      shadowCasterDrawItems);

  // The shadow pass's own Pipeline is bound first (P7's dependency-driven
  // compile order), and it is the only Pipeline that draws exactly
  // casterMesh's own 3 indices before the "draw" pass's own DrawItem
  // Pipeline is ever bound.
  REQUIRE(commandList.boundPipelines.size() == 3);
  CHECK(commandList.boundPipelines[0] == &shadowCastPipeline);
  CHECK(commandList.boundPipelines[1] != &shadowCastPipeline);
  REQUIRE(commandList.drawIndexedCounts.size() == 3);  // shadow caster, the one DrawItem, output-transform
  CHECK(commandList.drawIndexedCounts[0] == 3);        // casterMesh's own 3 indices

  const auto firstDrawIndexedIndex =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::DrawIndexed) -
      commandList.events.begin();
  REQUIRE(static_cast<std::size_t>(firstDrawIndexedIndex) < commandList.events.size());

  // The "draw" pass's own DrawItem Pipeline (boundPipelines[1]) is bound
  // via the SECOND BindPipeline event in the whole timeline -- strictly
  // after the shadow pass's own first (and only) DrawIndexed event, not
  // merely after its first event.
  const auto firstBindPipelineIndex =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::BindPipeline) -
      commandList.events.begin();
  const auto secondBindPipelineIndex =
      std::find(commandList.events.begin() + firstBindPipelineIndex + 1, commandList.events.end(),
                FakeCommandList::EventKind::BindPipeline) -
      commandList.events.begin();
  REQUIRE(static_cast<std::size_t>(secondBindPipelineIndex) < commandList.events.size());
  CHECK(firstDrawIndexedIndex < secondBindPipelineIndex);
}

TEST_CASE("The shadow-map bind lands at binding 2 for a plain PbrDirectLit DrawItem and binding 4 for an "
          "environmentBinding() == Ibl one, in the same frame (Plan 0027 Milestone 9, ADR-0072 D-1/P6)",
          "[renderer][ownership][shadow][pbr_ibl]") {
  atlantis::renderer::Mesh mesh(std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Vertex, 0),
                                 std::make_unique<FakeBuffer>(atlantis::rhi::BufferPurpose::Index, 0), 3);
  FakeSampledTexture baseColorA("base-color-a");
  FakeSampledTexture baseColorB("base-color-b");
  FakeSampledTexture environment("environment");
  FakeSampledTexture dfgLut("dfg-lut");
  FakeSampler baseColorSampler("base-color-sampler");
  FakeSampler environmentSampler("environment-sampler");
  FakeSampler dfgSampler("dfg-sampler");

  Material plainMaterial(std::make_unique<FakePipeline>(), atlantis::renderer::MaterialPushConstantLayout::PbrDirectLit,
                          &baseColorA, &baseColorSampler, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F, 0.5F,
                          MaterialEnvironmentBinding::None);
  Material iblMaterial(std::make_unique<FakePipeline>(), atlantis::renderer::MaterialPushConstantLayout::PbrDirectLit,
                        &baseColorB, &baseColorSampler, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F, 0.5F,
                        MaterialEnvironmentBinding::Ibl);

  DrawItem plainItem{.mesh = &mesh,
                      .material = &plainMaterial,
                      .objectToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  DrawItem iblItem{.mesh = &mesh,
                    .material = &iblMaterial,
                    .objectToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const std::vector<DrawItem> drawItems{plainItem, iblItem};

  FakeRenderTarget colorTarget("color");
  FakeTexture depthTarget("depth");
  FakeBuffer cameraBuffer(atlantis::rhi::BufferPurpose::Uniform, 592);
  FakeHdrColorTarget hdrColorTarget("hdr");
  FakeBuffer fullscreenVertexBuffer(atlantis::rhi::BufferPurpose::Vertex, 0);
  FakeBuffer fullscreenIndexBuffer(atlantis::rhi::BufferPurpose::Index, 0);
  FakePipeline outputTransformPipeline;
  FakeSampler outputTransformSampler("output-transform");
  const EnvironmentLighting lighting{environment, environmentSampler, dfgLut, dfgSampler};
  FakeShadowMap shadowMap("shadow_map");
  FakeSampler shadowMapSampler("shadow_map_sampler");
  FakePipeline shadowCastPipeline;
  FakeBuffer shadowLightSpaceBuffer(atlantis::rhi::BufferPurpose::Uniform, 0);

  FakeCommandList commandList;
  Renderer renderer;
  renderer.drawFrame(commandList, colorTarget, depthTarget, cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, hdrColorTarget, fullscreenVertexBuffer,
                      fullscreenIndexBuffer, outputTransformPipeline, outputTransformSampler, &lighting, nullptr,
                      shadowMap, shadowMapSampler, shadowCastPipeline, shadowLightSpaceBuffer, {});

  REQUIRE(commandList.boundShadowMapTextures.size() == 2);
  CHECK(commandList.boundShadowMapTextures[0].binding == 2);
  CHECK(commandList.boundShadowMapTextures[0].texture == &shadowMap);
  CHECK(commandList.boundShadowMapTextures[0].sampler == &shadowMapSampler);
  CHECK(commandList.boundShadowMapTextures[1].binding == 4);
  CHECK(commandList.boundShadowMapTextures[1].texture == &shadowMap);
  CHECK(commandList.boundShadowMapTextures[1].sampler == &shadowMapSampler);
}

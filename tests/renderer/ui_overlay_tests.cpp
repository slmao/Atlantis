#include <atlantis/renderer/renderer.h>
#include <atlantis/renderer/ui_overlay.h>
#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fake_command_list.h"

// Plan 0056 M3 (P5; ADR-0108 D3): Renderer::drawOverlay() records one
// "ui_overlay" pass -- the target cleared and left in the caller's final
// state, the list's vertices and indices written into the host's buffers, both
// textures bound once, then per command its scissor, push constant (texture
// slot, sRGB flag) and index range. No GPU: the RenderGraph tests' fake
// command list.

namespace {

using atlantis::render_graph::test::FakeBuffer;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakePipeline;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampledTexture;
using atlantis::render_graph::test::FakeSampler;
using atlantis::renderer::UiDrawCommand;
using atlantis::renderer::UiDrawList;
using atlantis::renderer::UiTexture;
using atlantis::renderer::UiVertex;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::Rect2D;
using atlantis::rhi::ResourceState;
using EventKind = FakeCommandList::EventKind;

struct PushConstants {
  float scaleX;
  float scaleY;
  std::uint32_t linearize;
  std::uint32_t slot;
};

}  // namespace

TEST_CASE("the UI vertex layout matches renderer::UiVertex", "[renderer][ui_overlay]") {
  const auto layout = atlantis::renderer::uiVertexInputLayout();
  CHECK(layout.strideBytes == sizeof(UiVertex));
  REQUIRE(layout.attributes.size() == 3);
  CHECK(layout.attributes[0].location == 0);
  CHECK(layout.attributes[0].offsetBytes == offsetof(UiVertex, x));
  CHECK(layout.attributes[0].format == atlantis::rhi::VertexAttributeFormat::Float2);
  CHECK(layout.attributes[1].offsetBytes == offsetof(UiVertex, u));
  CHECK(layout.attributes[1].format == atlantis::rhi::VertexAttributeFormat::Float2);
  CHECK(layout.attributes[2].offsetBytes == offsetof(UiVertex, r));
  CHECK(layout.attributes[2].format == atlantis::rhi::VertexAttributeFormat::Float4);
}

TEST_CASE("drawOverlay() records one pass: buffers written, textures bound once, per-command scissor and range",
          "[renderer][ui_overlay]") {
  UiDrawList list;
  list.display = {200, 100};
  for (int i = 0; i < 8; ++i) list.vertices.push_back(UiVertex{static_cast<float>(i), 0.0f});
  list.indices = {0, 1, 2, 0, 2, 3, 0, 1, 2};
  list.commands.push_back(UiDrawCommand{0, 6, 0, Rect2D{1, 2, {30, 40}}, UiTexture::FontAtlas});
  list.commands.push_back(UiDrawCommand{6, 3, 4, Rect2D{50, 0, {10, 10}}, UiTexture::Viewport});
  list.commands.push_back(UiDrawCommand{0, 3, 0, Rect2D{0, 0, {0, 10}}, UiTexture::FontAtlas});  // empty clip: skipped

  FakeBuffer vertices(BufferPurpose::Vertex, atlantis::renderer::uiVertexBytes(list));
  FakeBuffer indices(BufferPurpose::Index, atlantis::renderer::uiIndexBytes(list), atlantis::rhi::IndexType::Uint32);
  FakePipeline pipeline;
  FakeSampledTexture atlas("atlas");
  FakeSampler sampler("sampler");
  FakeRenderTarget viewport("viewport");
  FakeRenderTarget target("target");
  FakeCommandList commandList;
  const atlantis::renderer::UiOverlayResources resources{pipeline, vertices, indices, atlas, sampler,
                                                         viewport, sampler, /*linearizeVertexColors=*/true};
  atlantis::renderer::Renderer{}.drawOverlay(commandList, target, ResourceState::PresentSource, list, resources);

  CHECK(std::memcmp(vertices.mappedData(), list.vertices.data(), atlantis::renderer::uiVertexBytes(list)) == 0);
  CHECK(std::memcmp(indices.mappedData(), list.indices.data(), atlantis::renderer::uiIndexBytes(list)) == 0);

  // The target: Undefined -> ColorAttachmentOutput, cleared, -> the final state.
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.beginRenderingCalls[0].color == &target);
  REQUIRE(commandList.transitions.size() == 2);
  CHECK(commandList.transitions[1].after == ResourceState::PresentSource);

  REQUIRE(commandList.boundPipelines.size() == 1);
  REQUIRE(commandList.boundTextures.size() == 1);
  CHECK(commandList.boundTextures[0].binding == atlantis::renderer::kUiOverlayFontAtlasBinding);
  REQUIRE(commandList.boundRenderTargetTextures.size() == 1);
  CHECK(commandList.boundRenderTargetTextures[0].binding == atlantis::renderer::kUiOverlayViewportBinding);
  CHECK(commandList.boundRenderTargetTextures[0].target == &viewport);

  REQUIRE(commandList.scissors.size() == 2);
  CHECK(commandList.scissors[0] == Rect2D{1, 2, {30, 40}});
  CHECK(commandList.scissors[1] == Rect2D{50, 0, {10, 10}});
  REQUIRE(commandList.rangedDraws.size() == 2);
  CHECK(commandList.rangedDraws[0].indexCount == 6);
  CHECK(commandList.rangedDraws[0].firstIndex == 0);
  CHECK(commandList.rangedDraws[0].vertexOffset == 0);
  CHECK(commandList.rangedDraws[1].indexCount == 3);
  CHECK(commandList.rangedDraws[1].firstIndex == 6);
  CHECK(commandList.rangedDraws[1].vertexOffset == 4);
  CHECK(commandList.drawIndexedCounts.empty());

  REQUIRE(commandList.pushConstantData.size() == 2);
  PushConstants first{};
  PushConstants second{};
  REQUIRE(commandList.pushConstantData[0].size() == sizeof(PushConstants));
  std::memcpy(&first, commandList.pushConstantData[0].data(), sizeof(PushConstants));
  std::memcpy(&second, commandList.pushConstantData[1].data(), sizeof(PushConstants));
  CHECK(first.scaleX == 2.0f / 200.0f);
  CHECK(first.scaleY == 2.0f / 100.0f);
  CHECK(first.linearize == 1);
  CHECK(first.slot == 0);
  CHECK(second.slot == 1);

  // Scissor, then push constant, then the draw, per command, after the binds.
  std::vector<EventKind> tail;
  for (const EventKind kind : commandList.events) {
    if (kind == EventKind::SetScissor || kind == EventKind::PushConstant || kind == EventKind::DrawIndexedRange) {
      tail.push_back(kind);
    }
  }
  CHECK(tail == std::vector<EventKind>{EventKind::SetScissor, EventKind::PushConstant, EventKind::DrawIndexedRange,
                                       EventKind::SetScissor, EventKind::PushConstant, EventKind::DrawIndexedRange});
}

TEST_CASE("drawOverlay() with an empty list still clears the target and leaves it in the final state",
          "[renderer][ui_overlay]") {
  UiDrawList list;
  FakeBuffer vertices(BufferPurpose::Vertex, 0);
  FakeBuffer indices(BufferPurpose::Index, 0, atlantis::rhi::IndexType::Uint32);
  FakePipeline pipeline;
  FakeSampledTexture atlas("atlas");
  FakeSampler sampler("sampler");
  FakeRenderTarget viewport("viewport");
  FakeRenderTarget target("target");
  FakeCommandList commandList;
  const atlantis::renderer::UiOverlayResources resources{pipeline, vertices, indices, atlas, sampler, viewport, sampler};
  atlantis::renderer::Renderer{}.drawOverlay(commandList, target, ResourceState::TransferSource, list, resources);
  CHECK(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.boundPipelines.empty());
  REQUIRE(commandList.transitions.size() == 2);
  CHECK(commandList.transitions[1].after == ResourceState::TransferSource);
}

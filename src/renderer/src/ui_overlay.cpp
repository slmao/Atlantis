#include <atlantis/renderer/ui_overlay.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/renderer.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace atlantis::renderer {

namespace {

// shaders/editor_ui/editor_ui.slang's UiPushConstants, byte for byte: target
// pixels -> clip space (clip = position * scale - 1), whether to linearize the
// vertex colour, and which texture the draw samples.
struct UiOverlayPushConstants {
  float scaleX = 0.0f;
  float scaleY = 0.0f;
  std::uint32_t linearizeVertexColor = 0;
  std::uint32_t textureSlot = 0;  // 0: the font atlas, 1: the Viewport
};
static_assert(sizeof(UiOverlayPushConstants) == kUiOverlayPushConstantSizeBytes);

}  // namespace

atlantis::rhi::VertexInputLayout uiVertexInputLayout() {
  return atlantis::rhi::VertexInputLayout{
      .strideBytes = sizeof(UiVertex),
      .attributes = {
          {.location = 0, .offsetBytes = offsetof(UiVertex, x), .format = atlantis::rhi::VertexAttributeFormat::Float2},
          {.location = 1, .offsetBytes = offsetof(UiVertex, u), .format = atlantis::rhi::VertexAttributeFormat::Float2},
          {.location = 2, .offsetBytes = offsetof(UiVertex, r), .format = atlantis::rhi::VertexAttributeFormat::Float4},
      }};
}

std::size_t uiVertexBytes(const UiDrawList& list) noexcept { return list.vertices.size() * sizeof(UiVertex); }

std::size_t uiIndexBytes(const UiDrawList& list) noexcept { return list.indices.size() * sizeof(std::uint32_t); }

// Plan 0056 P5 (ADR-0108 D3): one RenderGraph pass, "ui_overlay", clearing
// the target and drawing the list's commands in order, each with its own
// scissor, texture slot and index range. Both textures are bound once (each
// descriptor written once per recording); the push constant selects which one
// a draw samples.
void Renderer::drawOverlay(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& target,
                           atlantis::rhi::ResourceState finalState, const UiDrawList& list,
                           const UiOverlayResources& resources) {
  ATLANTIS_CHECK_MSG(resources.vertexBuffer.purpose() == atlantis::rhi::BufferPurpose::Vertex &&
                         resources.indexBuffer.purpose() == atlantis::rhi::BufferPurpose::Index &&
                         resources.indexBuffer.indexType() == atlantis::rhi::IndexType::Uint32,
                     "drawOverlay(): the vertex/index buffers have the wrong purpose or index type");
  ATLANTIS_CHECK_MSG(resources.vertexBuffer.sizeBytes() >= uiVertexBytes(list) &&
                         resources.indexBuffer.sizeBytes() >= uiIndexBytes(list),
                     "drawOverlay(): the vertex/index buffers are smaller than the draw list");
  if (!list.vertices.empty()) {
    std::memcpy(resources.vertexBuffer.mappedData(), list.vertices.data(), uiVertexBytes(list));
  }
  if (!list.indices.empty()) {
    std::memcpy(resources.indexBuffer.mappedData(), list.indices.data(), uiIndexBytes(list));
  }

  const atlantis::rhi::Extent2D extent = target.extent();
  const float displayWidth = static_cast<float>(list.display.width != 0 ? list.display.width : extent.width);
  const float displayHeight = static_cast<float>(list.display.height != 0 ? list.display.height : extent.height);
  const float scaleX = displayWidth > 0.0f ? 2.0f / displayWidth : 0.0f;
  const float scaleY = displayHeight > 0.0f ? 2.0f / displayHeight : 0.0f;

  atlantis::render_graph::RenderGraphBuilder builder;
  const auto targetResource = builder.declareResource("ui-target");
  const auto pass = builder.declarePass("ui_overlay");
  builder.writes(pass, targetResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  builder.setExecute(pass, [&list, &resources, scaleX, scaleY](atlantis::rhi::CommandList& cmd) {
    if (list.commands.empty()) return;
    cmd.bindPipeline(resources.pipeline);
    cmd.bindVertexBuffer(resources.vertexBuffer);
    cmd.bindIndexBuffer(resources.indexBuffer);
    cmd.bindTexture(kUiOverlayFontAtlasBinding, resources.fontAtlas, resources.fontSampler);
    cmd.bindTexture(kUiOverlayViewportBinding, resources.viewport, resources.viewportSampler);
    for (const UiDrawCommand& command : list.commands) {
      if (command.indexCount == 0 || command.clip.extent.width == 0 || command.clip.extent.height == 0) continue;
      ATLANTIS_CHECK(static_cast<std::size_t>(command.firstIndex) + command.indexCount <= list.indices.size());
      const UiOverlayPushConstants payload{scaleX, scaleY, resources.linearizeVertexColors ? 1u : 0u,
                                           command.texture == UiTexture::Viewport ? 1u : 0u};
      cmd.setScissor(command.clip);
      cmd.pushConstant(&payload, sizeof(payload));
      cmd.drawIndexed(command.indexCount, command.firstIndex, command.vertexOffset);
    }
  });

  auto compiled = builder.compile();
  ATLANTIS_CHECK_MSG(compiled.isOk(), "the overlay's one-pass graph never fails to compile");
  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0),
       .target = &target,
       .colorClear = atlantis::rhi::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f},
       .finalState = finalState}};
  atlantis::render_graph::execute(compiled.value(), bindings, commandList);
}

}  // namespace atlantis::renderer

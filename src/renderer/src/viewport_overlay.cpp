#include <atlantis/renderer/viewport_overlay.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/renderer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace atlantis::renderer {

namespace {

using atlantis::rhi::AttachmentLoad;
using atlantis::rhi::ResourceState;

// shaders/viewport_overlay/viewport_overlay.slang's PushConstants, byte for byte.
struct ViewportOverlayPushConstants {
  float viewportWidth = 0.0f;
  float viewportHeight = 0.0f;
  std::uint32_t linearizeColor = 0;
  std::uint32_t padding = 0;
};
static_assert(sizeof(ViewportOverlayPushConstants) == kViewportOverlayPushConstantSizeBytes);

// shaders/selection_outline/selection_outline.slang's PushConstants, byte for byte.
struct SelectionOutlinePushConstants {
  std::array<float, 4> color{};
  float inverseWidth = 0.0f;
  float inverseHeight = 0.0f;
  std::uint32_t linearizeColor = 0;
  std::uint32_t padding = 0;
};
static_assert(sizeof(SelectionOutlinePushConstants) == kSelectionOutlinePushConstantSizeBytes);

// The shader's kinds (shape.x).
constexpr float kTriangle = 0.0f;
constexpr float kSegment = 1.0f;
constexpr float kPoint = 2.0f;

[[nodiscard]] std::array<float, 4> anchorOf(const ViewportOverlayAnchor& anchor) {
  return {anchor.position[0], anchor.position[1], anchor.position[2], anchor.pixelsPerUnit};
}

// A quad's four vertices, as two triangles {0, 1, 2} and {2, 1, 3}.
void appendQuad(ViewportOverlayPrimitives& into, const std::array<ViewportOverlayVertex, 4>& corners) {
  const auto first = static_cast<std::uint32_t>(into.vertices.size());
  into.vertices.insert(into.vertices.end(), corners.begin(), corners.end());
  for (const std::uint32_t i : {0u, 1u, 2u, 2u, 1u, 3u}) into.indices.push_back(first + i);
}

}  // namespace

atlantis::rhi::VertexInputLayout viewportOverlayVertexInputLayout() {
  using atlantis::rhi::VertexAttributeFormat;
  return atlantis::rhi::VertexInputLayout{
      .strideBytes = sizeof(ViewportOverlayVertex),
      .attributes = {
          {.location = 0, .offsetBytes = offsetof(ViewportOverlayVertex, position), .format = VertexAttributeFormat::Float3},
          {.location = 1, .offsetBytes = offsetof(ViewportOverlayVertex, other), .format = VertexAttributeFormat::Float3},
          {.location = 2, .offsetBytes = offsetof(ViewportOverlayVertex, color), .format = VertexAttributeFormat::Float4},
          {.location = 3, .offsetBytes = offsetof(ViewportOverlayVertex, shape), .format = VertexAttributeFormat::Float4},
          {.location = 4, .offsetBytes = offsetof(ViewportOverlayVertex, anchor), .format = VertexAttributeFormat::Float4},
      }};
}

void ViewportOverlayDrawList::addSegment(const std::array<float, 3>& a, const std::array<float, 3>& b,
                                         const std::array<float, 4>& color, ViewportOverlayDepth depth,
                                         float widthPixels, const ViewportOverlayAnchor& anchor) {
  // shape: y is the end (0 at a, 1 at b), z the side of the segment (-1, +1).
  const auto corner = [&](float end, float side) {
    return ViewportOverlayVertex{a, b, color, {kSegment, end, side, widthPixels}, anchorOf(anchor)};
  };
  appendQuad(depth == ViewportOverlayDepth::Tested ? tested : onTop,
             {corner(0.0f, -1.0f), corner(1.0f, -1.0f), corner(0.0f, 1.0f), corner(1.0f, 1.0f)});
}

void ViewportOverlayDrawList::addTriangle(const std::array<float, 3>& a, const std::array<float, 3>& b,
                                          const std::array<float, 3>& c, const std::array<float, 4>& color,
                                          ViewportOverlayDepth depth, const ViewportOverlayAnchor& anchor) {
  ViewportOverlayPrimitives& into = depth == ViewportOverlayDepth::Tested ? tested : onTop;
  const auto first = static_cast<std::uint32_t>(into.vertices.size());
  for (const auto& p : {a, b, c}) {
    into.vertices.push_back(ViewportOverlayVertex{p, {}, color, {kTriangle, 0.0f, 0.0f, 0.0f}, anchorOf(anchor)});
  }
  for (const std::uint32_t i : {0u, 1u, 2u}) into.indices.push_back(first + i);
}

void ViewportOverlayDrawList::addPoint(const std::array<float, 3>& p, const std::array<float, 4>& color,
                                       ViewportOverlayDepth depth, float sizePixels,
                                       const ViewportOverlayAnchor& anchor) {
  // shape: y, z are the corner of the square (-1, +1).
  const auto corner = [&](float x, float y) {
    return ViewportOverlayVertex{p, {}, color, {kPoint, x, y, sizePixels}, anchorOf(anchor)};
  };
  appendQuad(depth == ViewportOverlayDepth::Tested ? tested : onTop,
             {corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(-1.0f, 1.0f), corner(1.0f, 1.0f)});
}

std::size_t viewportOverlayVertexBytes(const ViewportOverlayDrawList& list) noexcept {
  return (list.tested.vertices.size() + list.onTop.vertices.size()) * sizeof(ViewportOverlayVertex);
}

std::size_t viewportOverlayIndexBytes(const ViewportOverlayDrawList& list) noexcept {
  return (list.tested.indices.size() + list.onTop.indices.size()) * sizeof(std::uint32_t);
}

// Plan 0066 P3 (ADR-0132 D3, ADR-0133 D5): the overlay's RenderGraph passes,
// after the scene's. The RenderGraph has one writer per resource per graph,
// so each pass on the target is its own graph, continuing the target (and
// the depth) from the end states the previous one reported:
//   selection_mask    -> the mask (cleared; ShaderRead after), then
//   selection_outline -> the target, loaded;
//   overlay_tested    -> the target and the frame's depth, both loaded;
//   overlay_on_top    -> the target, loaded.
// A pass with nothing to draw is not recorded; an empty list records nothing.
FrameEndStates Renderer::drawViewportOverlay(atlantis::rhi::CommandList& commandList,
                                             atlantis::rhi::RenderTarget& target, atlantis::rhi::Texture& depth,
                                             FrameEndStates incoming, atlantis::rhi::ResourceState finalColorState,
                                             const ViewportOverlayCamera& camera, const ViewportOverlayDrawList& list,
                                             const ViewportOverlayResources& resources) {
  if (list.empty()) return incoming;
  ATLANTIS_CHECK_MSG(resources.vertexBuffer.purpose() == atlantis::rhi::BufferPurpose::Vertex &&
                         resources.indexBuffer.purpose() == atlantis::rhi::BufferPurpose::Index &&
                         resources.indexBuffer.indexType() == atlantis::rhi::IndexType::Uint32 &&
                         resources.cameraBuffer.purpose() == atlantis::rhi::BufferPurpose::Uniform,
                     "drawViewportOverlay(): a buffer has the wrong purpose or index type");
  ATLANTIS_CHECK_MSG(resources.vertexBuffer.sizeBytes() >= viewportOverlayVertexBytes(list) &&
                         resources.indexBuffer.sizeBytes() >= viewportOverlayIndexBytes(list) &&
                         resources.cameraBuffer.sizeBytes() >= kViewportOverlayCameraSizeBytes,
                     "drawViewportOverlay(): a buffer is smaller than the draw list or the camera");

  // The camera, then the tested primitives followed by the on-top ones.
  auto* cameraData = static_cast<float*>(resources.cameraBuffer.mappedData());
  std::memcpy(cameraData, camera.view.data(), sizeof(camera.view));
  std::memcpy(cameraData + 16, camera.projection.data(), sizeof(camera.projection));
  auto* vertexData = static_cast<ViewportOverlayVertex*>(resources.vertexBuffer.mappedData());
  auto* indexData = static_cast<std::uint32_t*>(resources.indexBuffer.mappedData());
  if (!list.tested.vertices.empty()) {
    std::memcpy(vertexData, list.tested.vertices.data(), list.tested.vertices.size() * sizeof(ViewportOverlayVertex));
    std::memcpy(indexData, list.tested.indices.data(), list.tested.indices.size() * sizeof(std::uint32_t));
  }
  if (!list.onTop.vertices.empty()) {
    std::memcpy(vertexData + list.tested.vertices.size(), list.onTop.vertices.data(),
                list.onTop.vertices.size() * sizeof(ViewportOverlayVertex));
    std::memcpy(indexData + list.tested.indices.size(), list.onTop.indices.data(),
                list.onTop.indices.size() * sizeof(std::uint32_t));
  }

  const atlantis::rhi::Extent2D extent = target.extent();
  const ViewportOverlayPushConstants primitivePush{static_cast<float>(extent.width), static_cast<float>(extent.height),
                                                   resources.linearizeColors ? 1u : 0u, 0u};

  enum class Pass { Outline, Tested, OnTop };
  std::vector<Pass> passes;
  if (!list.selection.empty()) passes.push_back(Pass::Outline);
  if (!list.tested.indices.empty()) passes.push_back(Pass::Tested);
  if (!list.onTop.indices.empty()) passes.push_back(Pass::OnTop);

  FrameEndStates states = incoming;
  for (std::size_t i = 0; i < passes.size(); ++i) {
    // Every pass but the last leaves the target in ColorAttachmentOutput for the next to load.
    const ResourceState passFinal = i + 1 == passes.size() ? finalColorState : ResourceState::ColorAttachmentOutput;
    atlantis::render_graph::RenderGraphBuilder builder;
    const auto targetResource = builder.declareResource("overlay-target");
    std::vector<atlantis::render_graph::ResourceBinding> bindings;

    if (passes[i] == Pass::Outline) {
      ATLANTIS_CHECK_MSG(resources.mask.extent() == extent, "drawViewportOverlay(): the mask's extent is not the target's");
      // The mask, in its own graph: a RenderTarget cannot be read inside one (Guard 2).
      atlantis::render_graph::RenderGraphBuilder maskBuilder;
      const auto maskResource = maskBuilder.declareResource("selection-mask");
      const auto maskPass = maskBuilder.declarePass("selection_mask");
      maskBuilder.writes(maskPass, maskResource, ResourceState::ColorAttachmentOutput);
      maskBuilder.setExecute(maskPass, [&list, &resources](atlantis::rhi::CommandList& cmd) {
        cmd.bindPipeline(resources.maskPipeline);
        cmd.bindUniformBuffer(resources.cameraBuffer);
        for (const DrawItem& item : list.selection) {
          if (item.mesh == nullptr) continue;
          cmd.bindVertexBuffer(item.mesh->vertexBuffer());
          cmd.bindIndexBuffer(item.mesh->indexBuffer());
          cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size() * sizeof(float));
          cmd.drawIndexed(item.mesh->indexCount());
        }
      });
      auto maskCompiled = maskBuilder.compile();
      ATLANTIS_CHECK_MSG(maskCompiled.isOk(), "the selection mask's one-pass graph never fails to compile");
      static_cast<void>(atlantis::render_graph::execute(maskCompiled.value(),
                                                        {{.resource = maskCompiled.value().resourceAt(0),
                                                          .target = &resources.mask,
                                                          .colorClear = {0.0f, 0.0f, 0.0f, 0.0f},
                                                          .finalState = ResourceState::ShaderRead}},
                                                        commandList));

      const SelectionOutlinePushConstants outlinePush{kSelectionOutlineColor, 1.0f / static_cast<float>(extent.width),
                                                      1.0f / static_cast<float>(extent.height),
                                                      resources.linearizeColors ? 1u : 0u, 0u};
      const auto pass = builder.declarePass("selection_outline");
      builder.writes(pass, targetResource, ResourceState::ColorAttachmentOutput);
      builder.setExecute(pass, [&resources, outlinePush](atlantis::rhi::CommandList& cmd) {
        cmd.bindPipeline(resources.outlinePipeline);
        cmd.bindVertexBuffer(resources.fullscreenTriangleVertexBuffer);
        cmd.bindIndexBuffer(resources.fullscreenTriangleIndexBuffer);
        cmd.bindTexture(0, resources.mask, resources.maskSampler);
        cmd.pushConstant(&outlinePush, sizeof(outlinePush));
        cmd.drawIndexed(3);
      });
    } else {
      const bool tested = passes[i] == Pass::Tested;
      const auto pass = builder.declarePass(tested ? "overlay_tested" : "overlay_on_top");
      builder.writes(pass, targetResource, ResourceState::ColorAttachmentOutput);
      if (tested) {
        const auto depthResource = builder.declareResource("overlay-depth");
        builder.writes(pass, depthResource, ResourceState::DepthAttachmentReadWrite);
      }
      const auto indexCount = static_cast<std::uint32_t>(tested ? list.tested.indices.size() : list.onTop.indices.size());
      const auto firstIndex = static_cast<std::uint32_t>(tested ? 0 : list.tested.indices.size());
      const auto vertexOffset = static_cast<std::int32_t>(tested ? 0 : list.tested.vertices.size());
      builder.setExecute(pass, [&resources, tested, primitivePush, indexCount, firstIndex,
                                vertexOffset](atlantis::rhi::CommandList& cmd) {
        cmd.bindPipeline(tested ? resources.testedPipeline : resources.onTopPipeline);
        cmd.bindVertexBuffer(resources.vertexBuffer);
        cmd.bindIndexBuffer(resources.indexBuffer);
        cmd.bindUniformBuffer(resources.cameraBuffer);
        cmd.pushConstant(&primitivePush, sizeof(primitivePush));
        cmd.drawIndexed(indexCount, firstIndex, vertexOffset);
      });
    }

    auto compiled = builder.compile();
    ATLANTIS_CHECK_MSG(compiled.isOk(), "an overlay pass's graph never fails to compile");
    bindings.push_back({.resource = compiled.value().resourceAt(0),
                        .target = &target,
                        .incomingState = states.color,
                        .finalState = passFinal,
                        .load = AttachmentLoad::Load});
    const bool loadsDepth = passes[i] == Pass::Tested;
    if (loadsDepth) {
      bindings.push_back({.resource = compiled.value().resourceAt(1),
                          .depthTexture = &depth,
                          .incomingState = states.depth,
                          .finalState = std::nullopt,
                          .load = AttachmentLoad::Load});
    }
    const std::vector<ResourceState> endStates = atlantis::render_graph::execute(compiled.value(), bindings, commandList);
    states.color = endStates[0];
    if (loadsDepth) states.depth = endStates[1];
  }
  return states;
}

}  // namespace atlantis::renderer

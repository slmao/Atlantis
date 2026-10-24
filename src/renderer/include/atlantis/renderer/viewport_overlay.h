#pragma once

#include <atlantis/renderer/draw_item.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Plan 0066 P3 (ADR-0132 D3; Spec 0066 R3, R4): the Viewport overlay's input
// -- a Renderer-owned list of segments, triangles and points, each with a
// colour and a depth mode, in world groups or screen-constant groups, and the
// selected entities' draw items to outline. The Renderer knows neither the
// wire nor an editor: its host translates the control value into this list,
// as the --editor host translates ImGui's into UiDrawList.
namespace atlantis::renderer {

// Tested against the frame's depth (read only), or drawn on top of it.
enum class ViewportOverlayDepth : std::uint8_t { Tested, OnTop };

// A screen-constant group's anchor: its primitives' positions are offsets
// from `position`, one unit drawn as `pixelsPerUnit` pixels at the anchor's
// view depth, from the frame's camera. pixelsPerUnit 0 is a world group (the
// positions are world positions).
struct ViewportOverlayAnchor {
  std::array<float, 3> position{};
  float pixelsPerUnit = 0.0f;
};

// shaders/viewport_overlay's vertex: one layout for every primitive (see the
// shader's header). The layout is viewportOverlayVertexInputLayout().
struct ViewportOverlayVertex {
  std::array<float, 3> position{};  // triangle vertex, segment end A, or point
  std::array<float, 3> other{};     // segment end B
  std::array<float, 4> color{};     // straight alpha, sRGB-authored
  std::array<float, 4> shape{};     // kind (0, 1, 2), corner x/y or end/side, width or size in pixels
  std::array<float, 4> anchor{};    // anchor position, pixels per unit (0: world)
};

// The vertices and uint32 indices of one depth mode's primitives.
struct ViewportOverlayPrimitives {
  std::vector<ViewportOverlayVertex> vertices;
  std::vector<std::uint32_t> indices;
};

struct ViewportOverlayDrawList {
  ViewportOverlayPrimitives tested;
  ViewportOverlayPrimitives onTop;
  // The selected entities' draw items: each mesh at its current
  // objectToWorld (the material is not read). Outlined through occluders.
  std::vector<DrawItem> selection;

  // A segment as a screen-space quad of `widthPixels` (J5), with square caps.
  void addSegment(const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 4>& color,
                  ViewportOverlayDepth depth, float widthPixels, const ViewportOverlayAnchor& anchor = {});
  void addTriangle(const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c,
                   const std::array<float, 4>& color, ViewportOverlayDepth depth,
                   const ViewportOverlayAnchor& anchor = {});
  // A point as a screen-space square of `sizePixels`.
  void addPoint(const std::array<float, 3>& p, const std::array<float, 4>& color, ViewportOverlayDepth depth,
                float sizePixels, const ViewportOverlayAnchor& anchor = {});

  [[nodiscard]] bool empty() const noexcept {
    return tested.indices.empty() && onTop.indices.empty() && selection.empty();
  }
};

// The camera the frame drew from, column-major (as the scene's camera uniform).
struct ViewportOverlayCamera {
  std::array<float, 16> view{};
  std::array<float, 16> projection{};
};

// The selection outline's one colour (J4), straight alpha, sRGB-authored.
inline constexpr std::array<float, 4> kSelectionOutlineColor = {1.0f, 0.62f, 0.1f, 1.0f};

// shaders/viewport_overlay's vertex input: position Float3 @0, other Float3 @1,
// color Float4 @2, shape Float4 @3, anchor Float4 @4, stride
// sizeof(ViewportOverlayVertex).
[[nodiscard]] atlantis::rhi::VertexInputLayout viewportOverlayVertexInputLayout();

// The push constants' sizes (the structs are private to viewport_overlay.cpp).
inline constexpr std::size_t kViewportOverlayPushConstantSizeBytes = 16;
inline constexpr std::size_t kSelectionOutlinePushConstantSizeBytes = 32;
inline constexpr std::size_t kViewportOverlayCameraSizeBytes = sizeof(float) * 32;

// The host's resources for one drawViewportOverlay() call, all borrowed. The
// pipelines (created at the first non-empty overlay, J11), for the target's
// format:
// - testedPipeline / onTopPipeline: shaders/viewport_overlay with
//   viewportOverlayVertexInputLayout(), the camera uniform,
//   kViewportOverlayPushConstantSizeBytes, ColorBlendMode::AlphaBlend; Tested
//   with the depth attachment and depthWriteEnabled false, OnTop with no
//   depth attachment.
// - maskPipeline: shaders/selection_mask with the scene meshes' vertex layout
//   (the shadow pass's), the camera uniform, a 64-byte push constant, no
//   depth attachment, for mask's format.
// - outlinePipeline: shaders/selection_outline with the full-screen triangle's
//   layout (Float2 @0), no camera uniform, one sampled texture,
//   kSelectionOutlinePushConstantSizeBytes, no depth attachment.
// Buffers: cameraBuffer (Uniform, at least kViewportOverlayCameraSizeBytes),
// vertexBuffer / indexBuffer (Vertex / Index with IndexType::Uint32, at least
// viewportOverlayVertexBytes() / viewportOverlayIndexBytes()); the call writes
// all three, so no earlier GPU work may still read them. The full-screen
// triangle's buffers are drawFrame()'s. mask: a RenderTarget of an
// OffscreenTarget created sampled, at the target's extent; the call clears and
// writes it, and leaves it in ShaderRead. linearizeColors: the target's format
// is sRGB (its store encodes), so the sRGB-authored colours are converted to
// linear first.
struct ViewportOverlayResources {
  atlantis::rhi::Pipeline& testedPipeline;
  atlantis::rhi::Pipeline& onTopPipeline;
  atlantis::rhi::Pipeline& maskPipeline;
  atlantis::rhi::Pipeline& outlinePipeline;
  atlantis::rhi::Buffer& cameraBuffer;
  atlantis::rhi::Buffer& vertexBuffer;
  atlantis::rhi::Buffer& indexBuffer;
  atlantis::rhi::Buffer& fullscreenTriangleVertexBuffer;
  atlantis::rhi::Buffer& fullscreenTriangleIndexBuffer;
  atlantis::rhi::RenderTarget& mask;
  const atlantis::rhi::Sampler& maskSampler;
  bool linearizeColors = false;
};

[[nodiscard]] std::size_t viewportOverlayVertexBytes(const ViewportOverlayDrawList& list) noexcept;
[[nodiscard]] std::size_t viewportOverlayIndexBytes(const ViewportOverlayDrawList& list) noexcept;

}  // namespace atlantis::renderer

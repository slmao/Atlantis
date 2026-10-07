#pragma once

#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Plan 0056 P5 (ADR-0108 D3): the UI overlay pass's input -- a Renderer-owned
// list of textured, vertex-coloured, scissored triangles. Renderer knows
// neither the UI library nor the editor: its host copies the editor's draw
// list (the same shape) into this one.
namespace atlantis::renderer {

// Position in target pixels (origin top left), texture coordinates, and a
// straight-alpha colour authored in sRGB, each channel in [0, 1]. The vertex
// layout is uiVertexInputLayout().
struct UiVertex {
  float x = 0.0f;
  float y = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

// The two textures a UI draw samples (UiOverlayResources).
enum class UiTexture : std::uint8_t { FontAtlas, Viewport };

struct UiDrawCommand {
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
  std::int32_t vertexOffset = 0;
  atlantis::rhi::Rect2D clip;
  UiTexture texture = UiTexture::FontAtlas;
};

struct UiDrawList {
  std::vector<UiVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<UiDrawCommand> commands;
  atlantis::rhi::Extent2D display;  // the pixel space vertices are in
};

// shaders/editor_ui's vertex input: position Float2 @0, uv Float2 @1, colour
// Float4 @2, stride sizeof(UiVertex).
[[nodiscard]] atlantis::rhi::VertexInputLayout uiVertexInputLayout();

// The overlay Pipeline's fixed shape, besides its shaders and colorFormat:
// two fragment combined image samplers (the font atlas at binding 0, the
// Viewport at 1), no camera uniform, no depth, straight-alpha blending, and a
// 16-byte push constant (UiOverlayPushConstants, private to ui_overlay.cpp).
inline constexpr std::uint32_t kUiOverlayFontAtlasBinding = 0;
inline constexpr std::uint32_t kUiOverlayViewportBinding = 1;
inline constexpr std::uint32_t kUiOverlaySampledTextureBindingCount = 2;
inline constexpr std::size_t kUiOverlayPushConstantSizeBytes = 16;

// The host's resources for one drawOverlay() call, all borrowed.
// - pipeline: built from shaders/editor_ui for the target's format with the
//   shape above (ColorBlendMode::AlphaBlend, hasCameraUniformBinding and
//   hasDepthAttachment false).
// - vertexBuffer / indexBuffer: Vertex / Index (IndexType::Uint32) purpose,
//   at least uiVertexBytes() / uiIndexBytes() of the list; drawOverlay()
//   writes them, so no earlier GPU work may still read them.
// - fontAtlas: in ResourceState::ShaderRead; viewport: a RenderTarget of an
//   OffscreenTarget created sampled, in ResourceState::ShaderRead.
// - linearizeVertexColors: the target's format is sRGB (its store encodes),
//   so the shader converts the sRGB-authored vertex colours to linear first.
//   The Viewport is sampled through a view of its own format and written
//   back unchanged either way (Plan 0056 J6).
struct UiOverlayResources {
  atlantis::rhi::Pipeline& pipeline;
  atlantis::rhi::Buffer& vertexBuffer;
  atlantis::rhi::Buffer& indexBuffer;
  const atlantis::rhi::SampledTexture& fontAtlas;
  const atlantis::rhi::Sampler& fontSampler;
  const atlantis::rhi::RenderTarget& viewport;
  const atlantis::rhi::Sampler& viewportSampler;
  bool linearizeVertexColors = false;
};

[[nodiscard]] std::size_t uiVertexBytes(const UiDrawList& list) noexcept;
[[nodiscard]] std::size_t uiIndexBytes(const UiDrawList& list) noexcept;

}  // namespace atlantis::renderer

#pragma once

#include <cstdint>
#include <vector>

// Plan 0056 P5/P8 (ADR-0107, ADR-0108 D2/D3): the editor's UI output, an
// engine-neutral list of textured, vertex-coloured, scissored triangles. The
// editor's view layer fills it from the UI library's draw data; the host
// copies it into the Renderer's own list of the same shape. No UI-library or
// GPU type appears here.
namespace atlantis::editor {

// Position in framebuffer pixels (origin top left), texture coordinates, and
// a straight-alpha colour authored in sRGB, each channel in [0, 1].
struct UiVertex {
  float x = 0.0f;
  float y = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
  friend bool operator==(const UiVertex&, const UiVertex&) = default;
};

// The two textures a UI draw can sample: the font atlas (Editor::fontAtlas())
// and the host's Viewport image.
enum class UiTexture : std::uint8_t { FontAtlas, Viewport };

// A clip rectangle in framebuffer pixels.
struct UiRect {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  friend bool operator==(const UiRect&, const UiRect&) = default;
};

// indices[firstIndex, firstIndex + indexCount), each index added to
// vertexOffset, clipped to `clip`, sampling `texture`.
struct UiDrawCommand {
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
  std::int32_t vertexOffset = 0;
  UiRect clip;
  UiTexture texture = UiTexture::FontAtlas;
  friend bool operator==(const UiDrawCommand&, const UiDrawCommand&) = default;
};

// Commands draw in order, over a display of displayWidth x displayHeight.
struct UiDrawList {
  std::vector<UiVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<UiDrawCommand> commands;
  std::uint32_t displayWidth = 0;
  std::uint32_t displayHeight = 0;
  friend bool operator==(const UiDrawList&, const UiDrawList&) = default;
};

}  // namespace atlantis::editor

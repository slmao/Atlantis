#pragma once

#include <atlantis/platform/platform_event.h>
#include <atlantis/renderer/ui_overlay.h>
#include <atlantis/rhi/types.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Spec 0056 / ADR-0108 D1-D4 (Plan 0056 P6): what RuntimeApplication's frame
// needs from an attached overlay -- an editor, hosted by the atlantis_runtime
// executable (`--editor`). With one attached, the frame hands it every
// Platform event, renders the scene into an offscreen Viewport target at the
// overlay's size, and draws the overlay's UI over the swapchain image,
// sampling that target. The interface names only Platform and Renderer
// values, which Runtime already depends on: the host library knows no editor.
namespace atlantis::runtime {

// RGBA8 pixels, row-major, tightly packed; borrowed from the overlay.
struct FontAtlasPixels {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::span<const std::uint8_t> rgba;
};

// Plan 0062 P2 (Spec 0062 R3; ADR-0123 D1, D3): a camera to render the
// Viewport from in place of the scene's active camera's pose and lens. The
// eye is column 3 of the world matrix and the forward direction -column 2, as
// extractCameraMatrices() reads a camera; exposure, fog and bloom stay the
// active camera's.
struct ViewportCameraOverride {
  std::array<float, 16> worldMatrix{};  // column-major
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
};

// Not thread-safe: the frame thread only (ADR-0004).
class FrameOverlay {
 public:
  virtual ~FrameOverlay() = default;

  // Every event runFrame()'s Platform pump returns, in order, before the
  // frame acts on it.
  virtual void onPlatformEvent(const atlantis::platform::PlatformEvent& event) = 0;

  // The size to render the scene at: the Viewport panel's. The frame uses at
  // least 1x1.
  [[nodiscard]] virtual atlantis::rhi::Extent2D viewportExtent() const = 0;

  // The UI drawn over the swapchain image this frame, in the swapchain's
  // pixels; valid until the overlay next changes it, between frames.
  [[nodiscard]] virtual const atlantis::renderer::UiDrawList& drawList() const = 0;

  // Read once, by RuntimeApplication::attachOverlay(), and uploaded once.
  [[nodiscard]] virtual FontAtlasPixels fontAtlas() const = 0;

  // Plan 0062 P2: the camera the Viewport is rendered from this frame, or
  // none for the scene's active camera. Read by runFrame() and by the capture
  // path, which report and render the camera drawn (ADR-0123 D3). An override
  // the frame cannot use (non-finite, a degenerate direction, a lens outside
  // 0 < near < far and 0 < fov < pi) is ignored for the active camera.
  [[nodiscard]] virtual std::optional<ViewportCameraOverride> viewportCamera() const { return std::nullopt; }
};

// The overlay pass's shader pair (shaders/editor_ui), as SPIR-V files.
struct OverlayShaderPaths {
  std::string_view vertexSpirvPath;
  std::string_view fragmentSpirvPath;
};

enum class OverlayAttachError {
  AlreadyAttached,       // detachOverlay() first
  NoDevice,              // the application never initialized its Device
  ShaderLoadFailed,      // a SPIR-V file is missing or malformed
  FontUploadFailed,      // the font atlas texture could not be created or uploaded
};

[[nodiscard]] std::string_view toString(OverlayAttachError error) noexcept;

}  // namespace atlantis::runtime

#pragma once

namespace atlantis::rhi {

struct Extent2D {
  unsigned int width = 0;
  unsigned int height = 0;
  [[nodiscard]] bool isZero() const { return width == 0 && height == 0; }
};

[[nodiscard]] bool operator==(const Extent2D& lhs, const Extent2D& rhs);

// Describes only the currently-selected swapchain surface format for this
// spec's read-only metadata query -- not a general resource-format system.
// A future Buffer/Texture spec is expected to introduce its own format
// concept, quite possibly superseding this enum's role rather than
// extending it in place.
enum class Format {
  Unknown,
  Bgra8Unorm,
  Bgra8Srgb,
  Rgba8Unorm,
  Rgba8Srgb,
};

struct SwapchainMetadata {
  unsigned int imageCount = 0;
  Format format = Format::Unknown;
  Extent2D extent;
};

enum class PresentationError {
  SurfaceLost,
  SwapchainCreationFailed,
  DeviceLost,
  Unknown,
};

// This round's one resource kind (a RenderTarget's color image) and
// nothing else -- see ADR-0020. Extending this set for buffers, depth
// attachments, or shader-read states is future work, gated on a real
// consumer.
enum class ResourceState {
  Undefined,
  ColorAttachmentWrite,
  PresentSource,
};

struct ClearColorValue {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

[[nodiscard]] bool operator==(const ClearColorValue& lhs, const ClearColorValue& rhs);

enum class CommandListCreateError {
  CommandBufferAllocationFailed,
};

enum class SubmitError {
  QueueSubmitFailed,
  DeviceLost,
};

}  // namespace atlantis::rhi

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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

// Spec 0006's one resource kind (a RenderTarget's color image) plus Spec
// 0007's real graphics-pipeline draw states -- see ADR-0020/ADR-0025.
// ColorAttachmentOutput/DepthAttachmentReadWrite are deliberately distinct
// in name and Vulkan Backend mapping from ColorAttachmentWrite (which
// remains scoped to Spec 0006's transfer-based clearColor() and is never
// reused for the real graphics-pipeline color-output-merger write this
// round introduces -- see resource_state_mapping.cpp and ADR-0025's
// Decision for why reusing ColorAttachmentWrite here would be a genuine
// layout-correctness bug, not merely a naming ambiguity).
enum class ResourceState {
  Undefined,
  ColorAttachmentWrite,       // unchanged (Spec 0006) -- clearColor()'s transfer-dst state, never reused here
  PresentSource,               // unchanged (Spec 0006)
  ColorAttachmentOutput,       // new (Spec 0007) -- real graphics-pipeline color-attachment-output write
  DepthAttachmentReadWrite,    // new (Spec 0007) -- depth-test read + depth-write, single writes() usage (ADR-0026)
};

// Spec 0007's one depth format. A single-variant enum, not a bare
// bool/constant, so a future spec adding a second depth format (e.g. with
// stencil) extends this enum rather than replacing a non-enum
// representation.
enum class DepthFormat {
  D32Sfloat,  // VK_FORMAT_D32_SFLOAT -- guaranteed VK_IMAGE_TILING_OPTIMAL depth-attachment support
              // per the Vulkan spec's mandatory format support table; no capability query needed.
};

// Spec 0007's vertex attributes (position, one per-vertex color attribute
// -- Spec 0007 Risks & Open Questions) are both a 3-float vector; this
// enum exists so VertexInputLayout (below) does not silently assume a
// single hardcoded format. No VertexAttributeFormat beyond Float3 this
// round -- deliberately narrow, matching Spec 0007's own minimal-material
// scope; a future spec adding a second attribute type (e.g. Float2 for
// UVs) extends this enum.
enum class VertexAttributeFormat {
  Float3,
};

enum class BufferPurpose {
  Vertex,
  Index,
  Uniform,
};

struct BufferCreateParams {
  BufferPurpose purpose = BufferPurpose::Vertex;
  std::size_t sizeBytes = 0;
};

[[nodiscard]] bool operator==(const BufferCreateParams& lhs, const BufferCreateParams& rhs);

struct TextureCreateParams {
  Extent2D extent;
  DepthFormat format = DepthFormat::D32Sfloat;  // this round's only Texture usage is a depth attachment
};

[[nodiscard]] bool operator==(const TextureCreateParams& lhs, const TextureCreateParams& rhs);

struct VertexAttribute {
  std::uint32_t location = 0;
  std::uint32_t offsetBytes = 0;
  VertexAttributeFormat format = VertexAttributeFormat::Float3;
};

[[nodiscard]] bool operator==(const VertexAttribute& lhs, const VertexAttribute& rhs);

struct VertexInputLayout {
  std::uint32_t strideBytes = 0;
  std::vector<VertexAttribute> attributes;  // this round: exactly 2 (position @0, color @1)
};

[[nodiscard]] bool operator==(const VertexInputLayout& lhs, const VertexInputLayout& rhs);

// RHI does not parse, validate, or reflect this -- see ADR-0027. A
// non-owning view; the caller (Material construction) owns the actual
// byte storage (loaded once from a checked-in .spv file) for at least as
// long as this call.
struct ShaderStageBytecode {
  const std::uint32_t* spirvWords = nullptr;  // SPIR-V's own natural 32-bit-word granularity
  std::size_t wordCount = 0;
};

struct PipelineCreateParams {
  ShaderStageBytecode vertexShader;
  ShaderStageBytecode fragmentShader;
  VertexInputLayout vertexInputLayout;
  Format colorFormat = Format::Unknown;          // matches the bound RenderTarget's format at Material construction time
  DepthFormat depthFormat = DepthFormat::D32Sfloat;
  std::size_t pushConstantSizeBytes = 0;          // this round: sizeof(float) * 16 (one 4x4 matrix)
};

// Why three distinct *CreateError enums below, not one shared
// ResourceCreateError: mirrors this codebase's existing precedent
// (CommandListCreateError distinct from SubmitError, Spec 0006) -- each
// creation call's caller switches on exactly the failure modes relevant
// to it, not a shared enum with cases that can never apply to a given
// call.

enum class BufferCreateError {
  AllocationFailed,
  BufferCreationFailed,
};

enum class TextureCreateError {
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};

enum class PipelineCreateError {
  ShaderModuleCreationFailed,
  DescriptorSetLayoutCreationFailed,
  DescriptorSetAllocationFailed,  // vkAllocateDescriptorSets against VulkanDevice's fixed-capacity
                                   // pool -- see the Plan's camera-uniform-binding design for why
                                   // this is a distinct enumerator, not folded into PipelineCreationFailed
  PipelineLayoutCreationFailed,
  PipelineCreationFailed,
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

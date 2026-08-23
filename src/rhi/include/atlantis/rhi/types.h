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

// Describes the currently-selected swapchain surface format for
// Presentation's read-only metadata query, and (Spec 0010/ADR-0038) an
// OffscreenTarget's color image creation-time format parameter -- still
// not a general resource-format system: these four values remain
// color-attachment-shaped only, Texture's separate DepthFormat enum is
// untouched, and Buffer/Pipeline gain no new format concept from this. A
// future Buffer/Texture spec is expected to introduce its own general
// format concept, quite possibly superseding this enum's role rather
// than extending it in place.
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
  TransferSource,              // new (Spec 0010) -- GPU-to-CPU readback copy source state (ADR-0040)
  TransferDestination,         // new (Spec 0016) -- CPU-to-GPU upload copy destination state (ADR-0056)
  ShaderRead,                  // new (Spec 0016) -- sampled-texture shader-read-only state (ADR-0056)
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
// single hardcoded format. A future spec adding a second attribute type
// (e.g. Float2 for UVs) extends this enum -- Spec 0016 is that spec.
enum class VertexAttributeFormat {
  Float3,
  Float2,  // new (Spec 0016) -- UV vertex attributes
};

enum class BufferPurpose {
  Vertex,
  Index,
  Uniform,
  Readback,  // new (Spec 0010) -- host-visible destination for CommandList::copyRenderTargetToBuffer() (ADR-0040)
  Staging,   // new (Spec 0016) -- host-visible source for CommandList::copyBufferToTexture() (ADR-0056)
};

// Spec 0016/ADR-0055: sampled-texture color-space contract, independent
// of the swapchain/offscreen-shaped Format enum above (whose own BGRA
// variants are meaningless for an authored texture). First two values
// only -- see ADR-0057.
enum class SampledTextureFormat {
  Rgba8Unorm,  // linear
  Rgba8Srgb,
};

// Spec 0016/ADR-0055: minimal Sampler filter contract -- no separate
// mip filter, since every SampledTexture has exactly one mip level
// this round.
enum class Filter {
  Nearest,
  Linear,
};

// Spec 0016/ADR-0055: minimal Sampler address-mode contract, applied to
// both U and V.
enum class AddressMode {
  Repeat,
  ClampToEdge,
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

// Spec 0016/ADR-0055: no mip-count field -- every SampledTexture this
// round has exactly one mip level (Human Review item 12); exposing an
// unused knob is deliberately avoided, matching DepthFormat's own
// precedent above.
struct SampledTextureCreateParams {
  Extent2D extent;
  SampledTextureFormat format = SampledTextureFormat::Rgba8Unorm;
};

[[nodiscard]] bool operator==(const SampledTextureCreateParams& lhs, const SampledTextureCreateParams& rhs);

struct SamplerCreateParams {
  Filter filter = Filter::Nearest;
  AddressMode addressMode = AddressMode::ClampToEdge;
};

[[nodiscard]] bool operator==(const SamplerCreateParams& lhs, const SamplerCreateParams& rhs);

// Spec 0010/ADR-0038: creation-time parameters for a headless OffscreenTarget's
// color image. format defaults to a real, usable value (Rgba8Unorm), not
// Format::Unknown -- toVkFormat() unconditionally asserts on Unknown, and
// every sibling *CreateParams struct above defaults its enum field to a
// real value for the same reason.
struct OffscreenTargetCreateParams {
  Extent2D extent;
  Format format = Format::Rgba8Unorm;
};

[[nodiscard]] bool operator==(const OffscreenTargetCreateParams& lhs, const OffscreenTargetCreateParams& rhs);

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
  // Spec 0016/ADR-0056: caller-derived from the shader's own real
  // ReflectionMetadata (whether it declares a combined-image-sampler
  // binding) -- the same "caller derives from reflection, passes plain
  // data" pattern vertexInputLayout above already uses. false (default)
  // reproduces today's exact one-binding descriptor-set-layout/pool
  // behavior unconditionally -- zero source change at any existing
  // call site.
  bool hasSampledTextureBinding = false;
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

// Spec 0016/ADR-0055: mirrors TextureCreateError exactly -- a
// SampledTexture's own color image creation is structurally identical
// (image + memory + view).
enum class SampledTextureCreateError {
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};

enum class SamplerCreateError {
  SamplerCreationFailed,
};

// Spec 0010/ADR-0038: mirrors TextureCreateError exactly -- an
// OffscreenTarget's color image creation is structurally identical
// (image + memory + view).
enum class OffscreenTargetCreateError {
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};

// Spec 0010/ADR-0038: OffscreenTarget::acquireTarget()'s Err channel --
// reserved for a genuine unrecoverable, environmental failure, mirroring
// PresentationError's own non-precondition variants. Calling
// acquireTarget() while a previously-vended borrow is still outstanding
// is a guaranteed-detectable programmer error (ATLANTIS_CHECK), not part
// of this Result::Err channel.
enum class OffscreenAcquireError {
  DeviceLost,
  Unknown,
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

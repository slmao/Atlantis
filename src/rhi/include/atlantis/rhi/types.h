#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
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

// Plan 0024 Milestone 1 (ADR-0068 D-2): the fixed, single-variant HDR
// scene-color-intermediate format -- one value this round, mirroring
// DepthFormat's own established "one variant, no capability query
// needed for the format's own existence -- a real, executed
// vkGetPhysicalDeviceFormatProperties() check still runs at creation
// time, HdrColorTargetCreateError below" precedent. Deliberately a
// separate enum from Format above, never a fifth Format value -- see
// PipelineCreateParams::colorFormat's own comment for why the two
// vocabularies stay structurally distinct.
enum class HdrFormat {
  Rgba16Float,  // VK_FORMAT_R16G16B16A16_SFLOAT
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
  Rgba16Float,
  Rg16Float,
};

enum class SampledTextureDimension {
  Texture2D,
  TextureCube,
};

// Minification/magnification filter. MipFilter below independently
// selects filtering between mip levels (Spec 0025/P2).
enum class Filter {
  Nearest,
  Linear,
};

enum class MipFilter {
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

// Spec 0025/P2 widens the original single-mip 2D contract while the
// defaults preserve that exact behavior.
struct SampledTextureCreateParams {
  Extent2D extent;
  SampledTextureFormat format = SampledTextureFormat::Rgba8Unorm;
  SampledTextureDimension dimension = SampledTextureDimension::Texture2D;
  std::uint32_t mipLevelCount = 1;
};

[[nodiscard]] bool operator==(const SampledTextureCreateParams& lhs, const SampledTextureCreateParams& rhs);

struct SamplerCreateParams {
  Filter filter = Filter::Nearest;
  AddressMode addressMode = AddressMode::ClampToEdge;
  MipFilter mipFilter = MipFilter::Nearest;
  float minLod = 0.0F;
  float maxLod = 0.0F;
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

// Plan 0024 Milestone 1 (ADR-0068 D-1): creation-time parameters for
// the new HdrColorTarget scene-color intermediate. format defaults to
// the one real, usable HdrFormat value, mirroring
// OffscreenTargetCreateParams's own identical "never a to-VkFormat
// invalid default" rationale above.
struct HdrColorTargetCreateParams {
  Extent2D extent;
  HdrFormat format = HdrFormat::Rgba16Float;
};

[[nodiscard]] bool operator==(const HdrColorTargetCreateParams& lhs, const HdrColorTargetCreateParams& rhs);

// Plan 0027 Milestone 1 (ADR-0072 D-1): creation-time parameters for the
// new ShadowMap resource -- a depth image usable both as a depth
// attachment (the shadow-casting pass writes it) and as a sampled
// texture (the main draw pass reads it), mirroring HdrColorTargetCreateParams's
// own shape. format defaults to the RHI's only DepthFormat value.
struct ShadowMapCreateParams {
  Extent2D extent;
  DepthFormat format = DepthFormat::D32Sfloat;
};

[[nodiscard]] bool operator==(const ShadowMapCreateParams& lhs, const ShadowMapCreateParams& rhs);

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
  // Plan 0024 Milestone 1 (ADR-0068 D-2/D-4): a structural one-of, not
  // a required Format field plus an optional HdrFormat override -- that
  // shape was rejected during this Plan's own final review as a real
  // illegal-state risk (both set, or neither) and a priority
  // convention to document and get wrong, not a structural guarantee.
  // std::variant<Format, HdrFormat> makes "exactly one of the two
  // format vocabularies" true by construction, matching this exact
  // module family's own established precedent for "structurally
  // exactly one of a closed set of kinds"
  // (render_graph::CompileError = std::variant<MultipleProducersError,
  // DependencyCycleError>; platform::PlatformEvent's own explicit
  // rationale, "std::variant chosen over a polymorphic event base,
  // consistent with atlantis::Result's existing value-type style").
  // Every geometry-pass Pipeline (every MaterialKind) now holds
  // HdrFormat::Rgba16Float here; the new output-transform Pipeline
  // pair alone holds a real Format, matching whichever final target it
  // renders into. Format::Unknown inside the Format alternative
  // remains the one pre-existing illegal *value* (toVkFormat(Format)
  // already asserts on it) -- unchanged, not a new concept.
  std::variant<Format, HdrFormat> colorFormat = Format::Unknown;
  DepthFormat depthFormat = DepthFormat::D32Sfloat;
  std::size_t pushConstantSizeBytes = 0;          // this round: sizeof(float) * 16 (one 4x4 matrix)
  // Spec 0016/ADR-0056: caller-derived from the shader's own real
  // ReflectionMetadata (how many contiguous combined-image-sampler
  // bindings it declares) -- the same "caller derives from reflection,
  // passes plain data" pattern vertexInputLayout above already uses.
  // The closed values for this Plan are 0, 1, and 3.
  std::uint32_t sampledTextureBindingCount = 0;
  // Plan 0024 Milestone 6: discovered during Implementation, not
  // anticipated by the Plan's own file list -- every existing Pipeline
  // this Device creates has always had an unconditional
  // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER at binding 0
  // (vulkan_device.cpp's own createPipeline()), but the new output-
  // transform shader contract (Milestone 3, ADR-0068 D-10,
  // outputTransformExpectedDescriptorContract()) declares binding 0 as
  // a Sampler with no uniform buffer at all -- creating that Pipeline
  // through the unmodified, always-uniform-at-0 layout is a genuine
  // descriptor-type mismatch against the shader module's own compiled
  // SPIR-V, not a hypothetical one. true (default) reproduces today's
  // exact layout unconditionally -- zero source change at any existing
  // call site; when false, no uniform-buffer binding is created at
  // all, and the sampled-texture range (when non-empty) moves from
  // index 1 down to index 0 -- the exact, and only, shape
  // the output-transform contract needs. Confirmed against Human
  // Review direction (chat, 2026-09-01) before this field was added:
  // widen createPipeline()'s own contract now, as a disclosed,
  // additive correction, rather than silently working around it.
  bool hasCameraUniformBinding = true;
  // Plan 0024 Milestone 6: discovered during Implementation alongside
  // hasCameraUniformBinding above, same root cause -- every existing
  // Pipeline this Device creates has always had depth test/write
  // unconditionally enabled and a real depthAttachmentFormat
  // (DepthFormat has exactly one value, D32Sfloat -- no "none"), but
  // the output-transform pass (Renderer's own second RenderGraph pass,
  // Milestone 5) binds no depth resource at all -- its own
  // ResourceBinding never sets .depthTexture, so beginRendering()
  // correctly passes depth == nullptr and VkRenderingInfo::
  // pDepthAttachment is nullptr for that pass. A Pipeline whose own
  // VkPipelineRenderingCreateInfo::depthAttachmentFormat names a real
  // format, bound during a render-pass instance with no depth
  // attachment, is a genuine Vulkan validation violation, not a
  // hypothetical one. true (default) reproduces today's exact
  // behavior unconditionally -- zero source change at any existing
  // call site; when false, depth test and depth write are both
  // disabled and depthAttachmentFormat is left VK_FORMAT_UNDEFINED.
  // Confirmed against Human Review direction (chat, 2026-09-01),
  // mirroring hasCameraUniformBinding's own identical resolution.
  bool hasDepthAttachment = true;
  // Plan 0026 Milestone 1 (ADR-0071 P4): independent of hasDepthAttachment
  // above -- meaningful only when hasDepthAttachment == true. The sky
  // Pipeline needs depth TESTING enabled (so opaque scene geometry drawn
  // after it still occludes correctly) but depth WRITING disabled (so it
  // never corrupts the depth buffer for those later draws) -- a real
  // combination hasDepthAttachment's own all-or-nothing boolean cannot
  // express. true (default) reproduces every existing Pipeline's current
  // always-write-when-tested behavior exactly -- zero source change at
  // any existing call site.
  bool depthWriteEnabled = true;
  // Plan 0027 Milestone 1 (ADR-0072 D-2): independent of hasDepthAttachment
  // above -- the shadow-casting Pipeline needs a depth attachment but no
  // color attachment at all, a combination hasDepthAttachment's own
  // all-or-nothing boolean cannot express (it only ever governed the
  // depth side). true (default) reproduces every existing Pipeline's
  // current one-color-attachment behavior exactly -- zero source change
  // at any existing call site; when false, no color attachment is
  // declared and colorFormat is ignored.
  bool hasColorAttachment = true;
};

struct SampledTextureUploadRegion {
  std::size_t bufferOffsetBytes = 0;
  std::uint32_t mipLevel = 0;
  std::uint32_t arrayLayer = 0;
  Extent2D extent;
};

[[nodiscard]] bool operator==(const SampledTextureUploadRegion& lhs, const SampledTextureUploadRegion& rhs);

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

// Missing format/image capabilities are runtime facts discovered from
// the physical device, never programmer errors (Spec 0025/P2).
enum class SampledTextureCreateError {
  FormatFeaturesUnsupported,
  ImageFormatUnsupported,
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

// Plan 0024 Milestone 1 (ADR-0068 D-1/D-2): mirrors TextureCreateError/
// OffscreenTargetCreateError's own identical shape, plus one new,
// narrow enumerator this creation call alone needs.
enum class HdrColorTargetCreateError {
  // A device's real, physical VkFormatFeatureFlags for HdrFormat's own
  // VkFormat lack a required bit -- a real runtime fact, discovered at
  // creation time, never a programmer error (never ATLANTIS_CHECK).
  // Checked, and returned, before any VkResult-producing Vulkan call.
  FormatFeaturesUnsupported,
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};

// Plan 0027 Milestone 1 (ADR-0072 D-1): mirrors HdrColorTargetCreateError's
// own shape exactly -- ShadowMap needs the identical real
// vkGetPhysicalDeviceFormatProperties() check (its own required
// combination of depth-attachment-plus-sampled feature bits is not
// unconditionally guaranteed for D32_SFLOAT on every conformant device).
enum class ShadowMapCreateError {
  FormatFeaturesUnsupported,
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

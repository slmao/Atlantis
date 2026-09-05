#pragma once

#include <variant>
#include <vector>

#include <atlantis/result.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

// The fixed, expected descriptor contract this round's Minimal Renderer
// shaders must match -- a single, Atlantis-Tools-owned constant (defined
// in descriptor_contract.cpp) hand-kept in sync with
// vulkan_device.cpp's own hard-coded createPipeline() binding layout.
// This is a stated, accepted single-source-of-truth risk, not a solved
// problem -- Spec 0008 scoped descriptor reflection to "validate a
// fixed contract," not "eliminate hand-authored duplication of it."
[[nodiscard]] std::vector<DescriptorBinding> minimalRendererExpectedDescriptorContract();

// Spec 0016/D6: the fixed, expected descriptor contract a textured
// Material's shaders must match -- two bindings, {set 0, binding 0,
// UniformBuffer, Vertex} (the same camera-uniform binding every shader
// contract in this codebase already uses) plus {set 0, binding 1,
// Sampler, Fragment} (the combined image sampler,
// PipelineCreateParams::sampledTextureBindingCount's own descriptor-set-
// layout binding). Additive alongside minimalRendererExpectedDescriptorContract()
// above, which is untouched -- an untextured Material's shaders continue
// validating against that one, unaffected by this new contract's
// existence.
[[nodiscard]] std::vector<DescriptorBinding> texturedMaterialExpectedDescriptorContract();

// Plan 0019 Section P13 / ADR-0062 Decision 2: the fixed, expected
// descriptor contract the lit_textured shader pair must match -- three
// bindings: {set 0, binding 0, UniformBuffer, Vertex} and {set 0,
// binding 0, UniformBuffer, Fragment} (the *same* Vulkan binding,
// reflected separately from each stage's own separately-loaded
// reflection JSON -- ReflectionMetadata is per-stage, so the one real
// Vulkan binding legitimately appears as two entries here, one per
// stage that references it) plus {set 0, binding 1, Sampler, Fragment}
// (unchanged from the textured contract). Additive alongside
// texturedMaterialExpectedDescriptorContract() above, which is
// untouched.
[[nodiscard]] std::vector<DescriptorBinding> litTexturedExpectedDescriptorContract();

// Plan 0023 Milestone 3 (ADR-0067 D-3/D-4): the fixed, expected
// descriptor contract the pbr_direct_lit shader pair must match --
// identical shape to litTexturedExpectedDescriptorContract() above (the
// same Camera/Lighting/CameraWorldPosition uniform buffer bound to both
// stages, plus the same one combined image sampler on the fragment
// stage) -- PbrDirectLit shares the existing single-texture Material
// architecture and reads camera/lighting data from its fragment stage
// exactly as lit_textured already does. A separate, named function
// (not a reuse of litTexturedExpectedDescriptorContract() itself) to
// keep each MaterialKind's own contract independently named and
// independently callable, matching this header's own existing
// per-kind pattern.
[[nodiscard]] std::vector<DescriptorBinding> pbrDirectLitExpectedDescriptorContract();

// Spec 0025/P3: the IBL variant retains the uniform/base-color bindings
// and adds contiguous fragment samplers for the prefiltered cubemap and
// DFG LUT at bindings 2 and 3.
[[nodiscard]] std::vector<DescriptorBinding> pbrIblExpectedDescriptorContract();

// Plan 0024 Milestone 3 (ADR-0068 D-10): the fixed, expected descriptor
// contract BOTH output-transform shader pairs (output-transform-unorm
// and output-transform-srgb) must match -- exactly one binding, {set 0,
// binding 0, Sampler, Fragment}, sampling the HdrColorTarget. No
// uniform buffer (fixed exposure is a shader-compile-time constant, not
// per-frame state) and no push constant (the fullscreen triangle needs
// no per-draw transform) -- a smaller, simpler contract than every
// MaterialKind's own. One function, not two -- both variants share this
// identical shape; only their own fragment-stage math differs.
[[nodiscard]] std::vector<DescriptorBinding> outputTransformExpectedDescriptorContract();

// Plan 0026 Milestone 4 (ADR-0071 P3): the sky's own fixed, expected
// descriptor contract -- two bindings, both Fragment-only: {set 0,
// binding 0, UniformBuffer, Fragment} (the existing frame uniform,
// referenced only by fragmentMain's own ray reconstruction -- vertexMain
// touches no resource) and {set 0, binding 1, Sampler, Fragment} (the
// environment cubemap). Confirmed, not inferred: a disposable Plan-stage
// probe compiled with the real slangc toolchain showed the vertex
// stage's own reflection reports both resources "used": 0 (dropped
// before reaching ReflectionMetadata::descriptorBindings,
// slang_json_transform.cpp's own `if (!used) continue;`), while the
// fragment stage reports both "used": 1 -- unlike pbrIblExpectedDescriptorContract()'s
// own binding 0, which IS Vertex+Fragment because pbr_ibl's own vertex
// stage does read the camera uniform for position/normal transforms.
[[nodiscard]] std::vector<DescriptorBinding> skyExpectedDescriptorContract();

// Plan 0027 Milestone 4 (ADR-0072 D-3): shadow_cast's own fixed, expected
// descriptor contract -- one binding, Vertex-only: {set 0, binding 0,
// UniformBuffer, Vertex} (the dedicated light-space buffer, referenced
// only by vertexMain -- fragmentMain is void, no bindings at all).
// Confirmed, not inferred: a disposable probe compiled with the real
// slangc toolchain during this Plan's own drafting showed the vertex
// stage's own reflection reports lightSpace "used": 1, while the
// fragment stage reports it "used": 0 (dropped before reaching
// ReflectionMetadata::descriptorBindings) and has no result/color output
// at all.
[[nodiscard]] std::vector<DescriptorBinding> shadowCastExpectedDescriptorContract();

enum class ContractMismatchError {
  BindingCountMismatch,
  BindingNotFound,        // an expected {set, binding} pair is absent from the reflected shader
  DescriptorTypeMismatch,
  StageMismatch,
  UnexpectedExtraBinding,  // the shader declares a binding the expected contract does not
};

// Compares metadata's own descriptorBindings against `expected`,
// returning Result<..., ...> -- validation only (ADR-0030), never
// constructs or returns anything RHI-shaped. Called by
// atlantis_shader_compiler at build time.
//
// Deviation from Plan 0008 (mechanical, no architectural effect): see
// reflection_loader.h's own note -- Result<void, E> is not
// instantiable against atlantis::Result<T, E>'s std::variant<T, E>
// storage, so this uses std::monostate, matching
// atlantis::rhi::Device::waitIdle()'s existing precedent.
[[nodiscard]] atlantis::Result<std::monostate, ContractMismatchError> validateDescriptorContract(
    const ReflectionMetadata& metadata, const std::vector<DescriptorBinding>& expected);

}  // namespace atlantis::shader_system

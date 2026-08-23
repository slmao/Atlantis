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
// PipelineCreateParams::hasSampledTextureBinding's own descriptor-set-
// layout binding). Additive alongside minimalRendererExpectedDescriptorContract()
// above, which is untouched -- an untextured Material's shaders continue
// validating against that one, unaffected by this new contract's
// existence.
[[nodiscard]] std::vector<DescriptorBinding> texturedMaterialExpectedDescriptorContract();

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

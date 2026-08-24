#include <atlantis/shader_system/descriptor_contract.h>

#include <algorithm>

namespace atlantis::shader_system {

// Hand-kept in sync with vulkan_backend/src/vulkan_device.cpp's own
// hard-coded createPipeline() binding layout: exactly one binding,
// VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, binding 0, vertex stage. See this
// header's own file-level comment and Plan 0008 Section 9's GPU-test
// regression backstop (PHR-0008-07) for why this duplication is a
// stated, accepted risk rather than a solved problem this round.
std::vector<DescriptorBinding> minimalRendererExpectedDescriptorContract() {
  return {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}};
}

// Hand-kept in sync with vulkan_backend/src/vulkan_device.cpp's own
// createPipeline() conditional second binding (Spec 0016/D5,
// PipelineCreateParams::hasSampledTextureBinding): binding 0 is the same
// camera-uniform binding every contract in this codebase uses; binding 1
// is the combined image sampler, fragment stage (ADR-0056 Decision 9).
std::vector<DescriptorBinding> texturedMaterialExpectedDescriptorContract() {
  return {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
          DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment}};
}

atlantis::Result<std::monostate, ContractMismatchError> validateDescriptorContract(
    const ReflectionMetadata& metadata, const std::vector<DescriptorBinding>& expected) {
  using ResultType = atlantis::Result<std::monostate, ContractMismatchError>;

  if (metadata.descriptorBindings.size() != expected.size()) {
    return ResultType::Err(ContractMismatchError::BindingCountMismatch);
  }

  for (const DescriptorBinding& expectedBinding : expected) {
    const auto match =
        std::find_if(metadata.descriptorBindings.begin(), metadata.descriptorBindings.end(),
                      [&](const DescriptorBinding& actual) {
                        return actual.set == expectedBinding.set && actual.binding == expectedBinding.binding;
                      });
    if (match == metadata.descriptorBindings.end()) return ResultType::Err(ContractMismatchError::BindingNotFound);
    if (match->type != expectedBinding.type) return ResultType::Err(ContractMismatchError::DescriptorTypeMismatch);
    if (match->stage != expectedBinding.stage) return ResultType::Err(ContractMismatchError::StageMismatch);
  }

  for (const DescriptorBinding& actualBinding : metadata.descriptorBindings) {
    const bool isExpected =
        std::any_of(expected.begin(), expected.end(), [&](const DescriptorBinding& expectedBinding) {
          return expectedBinding.set == actualBinding.set && expectedBinding.binding == actualBinding.binding;
        });
    if (!isExpected) return ResultType::Err(ContractMismatchError::UnexpectedExtraBinding);
  }

  return ResultType::Ok(std::monostate{});
}

}  // namespace atlantis::shader_system

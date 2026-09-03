#include <atlantis/renderer/material.h>

#include <atlantis/assert.h>

namespace atlantis::renderer {

Material::Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline, MaterialPushConstantLayout pushConstantLayout,
                    const atlantis::rhi::SampledTexture* sampledTexture, const atlantis::rhi::Sampler* sampler,
                    std::array<float, 4> baseColorFactor, float metallicFactor, float roughnessFactor,
                    MaterialEnvironmentBinding environmentBinding) noexcept
    : pipeline_(std::move(pipeline)),
      sampledTexture_(sampledTexture),
      sampler_(sampler),
      pushConstantLayout_(pushConstantLayout),
      baseColorFactor_(baseColorFactor),
      metallicFactor_(metallicFactor),
      roughnessFactor_(roughnessFactor),
      environmentBinding_(environmentBinding) {
  ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ == nullptr));
}

atlantis::Result<Material, CreateMaterialError> createMaterial(
    atlantis::rhi::Device& device, const atlantis::rhi::PipelineCreateParams& params,
    const atlantis::rhi::SampledTexture* sampledTexture, const atlantis::rhi::Sampler* sampler,
    MaterialPushConstantLayout pushConstantLayout, std::array<float, 4> baseColorFactor, float metallicFactor,
    float roughnessFactor, MaterialEnvironmentBinding environmentBinding) {
  using ResultT = atlantis::Result<Material, CreateMaterialError>;

  auto pipelineResult = device.createPipeline(params);
  if (pipelineResult.isErr()) {
    return ResultT::Err(CreateMaterialError::PipelineCreationFailed);
  }
  return ResultT::Ok(Material(std::move(pipelineResult.value()), pushConstantLayout, sampledTexture, sampler,
                               baseColorFactor, metallicFactor, roughnessFactor, environmentBinding));
}

}  // namespace atlantis::renderer

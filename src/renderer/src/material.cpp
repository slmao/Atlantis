#include <atlantis/renderer/material.h>

#include <atlantis/assert.h>

namespace atlantis::renderer {

Material::Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline, MaterialPushConstantLayout pushConstantLayout,
                    const atlantis::rhi::SampledTexture* sampledTexture, const atlantis::rhi::Sampler* sampler,
                    std::array<float, 4> baseColorFactor, float metallicFactor, float roughnessFactor,
                    MaterialEnvironmentBinding environmentBinding,
                    const atlantis::rhi::SampledTexture* normalMapTexture, float clearcoatFactor,
                    float clearcoatRoughness) noexcept
    : pipeline_(std::move(pipeline)),
      sampledTexture_(sampledTexture),
      sampler_(sampler),
      pushConstantLayout_(pushConstantLayout),
      baseColorFactor_(baseColorFactor),
      metallicFactor_(metallicFactor),
      roughnessFactor_(roughnessFactor),
      environmentBinding_(environmentBinding),
      normalMapTexture_(normalMapTexture),
      clearcoatFactor_(clearcoatFactor),
      clearcoatRoughness_(clearcoatRoughness) {
  ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ == nullptr));
  // Plan 0029 Section P14 (ADR-0074 Section 2): a normal map may never
  // be constructed without the base-color pair also present -- both
  // are sampled through the one, same sampler_.
  ATLANTIS_CHECK(normalMapTexture_ == nullptr || sampledTexture_ != nullptr);
  // A normal map may only be constructed on a Material whose push-
  // constant layout is PbrDirectLit or PbrClearcoat (Plan 0035
  // Milestone 2/ADR-0081 widening) -- the only layouts whose own
  // shaders declare a normal-map binding.
  ATLANTIS_CHECK(normalMapTexture_ == nullptr || pushConstantLayout_ == MaterialPushConstantLayout::PbrDirectLit ||
                 pushConstantLayout_ == MaterialPushConstantLayout::PbrClearcoat);
}

atlantis::Result<Material, CreateMaterialError> createMaterial(
    atlantis::rhi::Device& device, const atlantis::rhi::PipelineCreateParams& params,
    const atlantis::rhi::SampledTexture* sampledTexture, const atlantis::rhi::Sampler* sampler,
    MaterialPushConstantLayout pushConstantLayout, std::array<float, 4> baseColorFactor, float metallicFactor,
    float roughnessFactor, MaterialEnvironmentBinding environmentBinding,
    const atlantis::rhi::SampledTexture* normalMapTexture, float clearcoatFactor, float clearcoatRoughness) {
  using ResultT = atlantis::Result<Material, CreateMaterialError>;

  auto pipelineResult = device.createPipeline(params);
  if (pipelineResult.isErr()) {
    return ResultT::Err(CreateMaterialError::PipelineCreationFailed);
  }
  return ResultT::Ok(Material(std::move(pipelineResult.value()), pushConstantLayout, sampledTexture, sampler,
                               baseColorFactor, metallicFactor, roughnessFactor, environmentBinding, normalMapTexture,
                               clearcoatFactor, clearcoatRoughness));
}

}  // namespace atlantis::renderer

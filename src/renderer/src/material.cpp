#include <atlantis/renderer/material.h>

#include <atlantis/assert.h>

namespace atlantis::renderer {

Material::Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline, MaterialPushConstantLayout pushConstantLayout,
                    const atlantis::rhi::SampledTexture* sampledTexture, const atlantis::rhi::Sampler* sampler,
                    std::array<float, 4> baseColorFactor, float metallicFactor, float roughnessFactor,
                    MaterialEnvironmentBinding environmentBinding,
                    const atlantis::rhi::SampledTexture* normalMapTexture, float clearcoatFactor,
                    float clearcoatRoughness, std::array<float, 3> sheenColor, float sheenRoughness,
                    float anisotropyFactor, float anisotropyRotation, std::array<float, 3> emissiveFactor,
                    MaterialAlphaMode alphaMode) noexcept
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
      clearcoatRoughness_(clearcoatRoughness),
      sheenColor_(sheenColor),
      sheenRoughness_(sheenRoughness),
      anisotropyFactor_(anisotropyFactor),
      anisotropyRotation_(anisotropyRotation),
      emissiveFactor_(emissiveFactor),
      alphaMode_(alphaMode) {
  ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ == nullptr));
  // Plan 0029 Section P14 (ADR-0074 Section 2): a normal map may never
  // be constructed without the base-color pair also present -- both
  // are sampled through the one, same sampler_.
  ATLANTIS_CHECK(normalMapTexture_ == nullptr || sampledTexture_ != nullptr);
  // A normal map may only be constructed on a Material whose push-
  // constant layout is PbrDirectLit, PbrClearcoat, PbrSheen, or
  // PbrAnisotropic (Plan 0035 Milestones 2/3/4 ADR-0081 widening) --
  // the only layouts whose own shaders declare a normal-map binding.
  ATLANTIS_CHECK(normalMapTexture_ == nullptr || pushConstantLayout_ == MaterialPushConstantLayout::PbrDirectLit ||
                 pushConstantLayout_ == MaterialPushConstantLayout::PbrClearcoat ||
                 pushConstantLayout_ == MaterialPushConstantLayout::PbrSheen ||
                 pushConstantLayout_ == MaterialPushConstantLayout::PbrAnisotropic);
  // Plan 0042 Milestone 1 (Spec 0042 R2): only the PBR shaders can honour
  // a non-Opaque mode.
  ATLANTIS_CHECK(alphaMode_ == MaterialAlphaMode::Opaque ||
                 pushConstantLayout_ != MaterialPushConstantLayout::ObjectToWorldOnly);
}

atlantis::Result<Material, CreateMaterialError> createMaterial(
    atlantis::rhi::Device& device, const atlantis::rhi::PipelineCreateParams& params,
    const atlantis::rhi::SampledTexture* sampledTexture, const atlantis::rhi::Sampler* sampler,
    MaterialPushConstantLayout pushConstantLayout, std::array<float, 4> baseColorFactor, float metallicFactor,
    float roughnessFactor, MaterialEnvironmentBinding environmentBinding,
    const atlantis::rhi::SampledTexture* normalMapTexture, float clearcoatFactor, float clearcoatRoughness,
    std::array<float, 3> sheenColor, float sheenRoughness, float anisotropyFactor, float anisotropyRotation,
    std::array<float, 3> emissiveFactor, MaterialAlphaMode alphaMode) {
  using ResultT = atlantis::Result<Material, CreateMaterialError>;

  auto pipelineResult = device.createPipeline(params);
  if (pipelineResult.isErr()) {
    return ResultT::Err(CreateMaterialError::PipelineCreationFailed);
  }
  return ResultT::Ok(Material(std::move(pipelineResult.value()), pushConstantLayout, sampledTexture, sampler,
                               baseColorFactor, metallicFactor, roughnessFactor, environmentBinding, normalMapTexture,
                               clearcoatFactor, clearcoatRoughness, sheenColor, sheenRoughness, anisotropyFactor,
                               anisotropyRotation, emissiveFactor, alphaMode));
}

}  // namespace atlantis::renderer

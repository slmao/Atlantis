#include <atlantis/renderer/material.h>

#include <atlantis/assert.h>

namespace atlantis::renderer {

Material::Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline,
                    const atlantis::rhi::SampledTexture* sampledTexture,
                    const atlantis::rhi::Sampler* sampler) noexcept
    : pipeline_(std::move(pipeline)), sampledTexture_(sampledTexture), sampler_(sampler) {
  ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ == nullptr));
}

atlantis::Result<Material, CreateMaterialError> createMaterial(atlantis::rhi::Device& device,
                                                                 const atlantis::rhi::PipelineCreateParams& params,
                                                                 const atlantis::rhi::SampledTexture* sampledTexture,
                                                                 const atlantis::rhi::Sampler* sampler) {
  using ResultT = atlantis::Result<Material, CreateMaterialError>;

  auto pipelineResult = device.createPipeline(params);
  if (pipelineResult.isErr()) {
    return ResultT::Err(CreateMaterialError::PipelineCreationFailed);
  }
  return ResultT::Ok(Material(std::move(pipelineResult.value()), sampledTexture, sampler));
}

}  // namespace atlantis::renderer

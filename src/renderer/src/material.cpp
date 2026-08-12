#include <atlantis/renderer/material.h>

namespace atlantis::renderer {

Material::Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline) noexcept : pipeline_(std::move(pipeline)) {}

atlantis::Result<Material, CreateMaterialError> createMaterial(atlantis::rhi::Device& device,
                                                                 const atlantis::rhi::PipelineCreateParams& params) {
  using ResultT = atlantis::Result<Material, CreateMaterialError>;

  auto pipelineResult = device.createPipeline(params);
  if (pipelineResult.isErr()) {
    return ResultT::Err(CreateMaterialError::PipelineCreationFailed);
  }
  return ResultT::Ok(Material(std::move(pipelineResult.value())));
}

}  // namespace atlantis::renderer

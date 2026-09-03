#include <atlantis/runtime/environment_realization.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace atlantis::runtime {

atlantis::renderer::EnvironmentLighting EnvironmentLightingResources::borrowedView() const {
  ATLANTIS_CHECK(prefilteredEnvironment && environmentSampler && dfgLut && dfgSampler);
  return {*prefilteredEnvironment, *environmentSampler, *dfgLut, *dfgSampler};
}

atlantis::Result<EnvironmentLightingCandidate, EnvironmentRealizationError> realizeEnvironmentCandidate(
    atlantis::rhi::Device& device, const atlantis::asset_system::EnvironmentAssetData& environmentData) {
  using ResultT = atlantis::Result<EnvironmentLightingCandidate, EnvironmentRealizationError>;
  EnvironmentLightingCandidate candidate;
  candidate.resources.irradianceSh = environmentData.irradianceSh;

  auto environmentResult = device.createSampledTexture(
      {.extent = {environmentData.faceSize, environmentData.faceSize},
       .format = atlantis::rhi::SampledTextureFormat::Rgba16Float,
       .dimension = atlantis::rhi::SampledTextureDimension::TextureCube,
       .mipLevelCount = environmentData.mipCount});
  if (environmentResult.isErr()) {
    return ResultT::Err(EnvironmentRealizationError::PrefilteredTextureCreateFailed);
  }
  candidate.resources.prefilteredEnvironment = std::move(environmentResult.value());

  auto dfgResult = device.createSampledTexture(
      {.extent = {environmentData.dfgWidth, environmentData.dfgHeight},
       .format = atlantis::rhi::SampledTextureFormat::Rg16Float});
  if (dfgResult.isErr()) return ResultT::Err(EnvironmentRealizationError::DfgTextureCreateFailed);
  candidate.resources.dfgLut = std::move(dfgResult.value());

  auto environmentSamplerResult = device.createSampler(
      {.filter = atlantis::rhi::Filter::Linear,
       .addressMode = atlantis::rhi::AddressMode::ClampToEdge,
       .mipFilter = atlantis::rhi::MipFilter::Linear,
       .minLod = 0.0F,
       .maxLod = static_cast<float>(environmentData.mipCount - 1)});
  if (environmentSamplerResult.isErr()) {
    return ResultT::Err(EnvironmentRealizationError::EnvironmentSamplerCreateFailed);
  }
  candidate.resources.environmentSampler = std::move(environmentSamplerResult.value());

  auto dfgSamplerResult = device.createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (dfgSamplerResult.isErr()) return ResultT::Err(EnvironmentRealizationError::DfgSamplerCreateFailed);
  candidate.resources.dfgSampler = std::move(dfgSamplerResult.value());

  const std::size_t specularBytes = environmentData.specularRgba16Float.size() * sizeof(std::uint16_t);
  auto specularStagingResult =
      device.createBuffer({.purpose = atlantis::rhi::BufferPurpose::Staging, .sizeBytes = specularBytes});
  if (specularStagingResult.isErr()) {
    return ResultT::Err(EnvironmentRealizationError::SpecularStagingBufferCreateFailed);
  }
  candidate.specularStagingBuffer = std::move(specularStagingResult.value());
  std::memcpy(candidate.specularStagingBuffer->mappedData(), environmentData.specularRgba16Float.data(),
              specularBytes);

  const std::size_t dfgBytes = environmentData.dfgRg16Float.size() * sizeof(std::uint16_t);
  auto dfgStagingResult =
      device.createBuffer({.purpose = atlantis::rhi::BufferPurpose::Staging, .sizeBytes = dfgBytes});
  if (dfgStagingResult.isErr()) return ResultT::Err(EnvironmentRealizationError::DfgStagingBufferCreateFailed);
  candidate.dfgStagingBuffer = std::move(dfgStagingResult.value());
  std::memcpy(candidate.dfgStagingBuffer->mappedData(), environmentData.dfgRg16Float.data(), dfgBytes);

  std::size_t offsetBytes = 0;
  std::uint32_t mipSize = environmentData.faceSize;
  candidate.specularUploadRegions.reserve(static_cast<std::size_t>(environmentData.mipCount) * 6);
  for (std::uint32_t mip = 0; mip < environmentData.mipCount; ++mip) {
    const std::size_t faceBytes = static_cast<std::size_t>(mipSize) * mipSize * 4 * sizeof(std::uint16_t);
    for (std::uint32_t face = 0; face < 6; ++face) {
      candidate.specularUploadRegions.push_back(
          {.bufferOffsetBytes = offsetBytes, .mipLevel = mip, .arrayLayer = face, .extent = {mipSize, mipSize}});
      offsetBytes += faceBytes;
    }
    mipSize = mipSize > 1 ? mipSize / 2 : 1;
  }
  ATLANTIS_CHECK(offsetBytes == specularBytes);
  return ResultT::Ok(std::move(candidate));
}

void recordEnvironmentUploads(atlantis::rhi::CommandList& commandList,
                              EnvironmentLightingCandidate& candidate) {
  atlantis::render_graph::RenderGraphBuilder builder;
  const auto environmentResource = builder.declareResource("environment-specular-upload");
  const auto dfgResource = builder.declareResource("environment-dfg-upload");
  const auto environmentPass = builder.declarePass("EnvironmentSpecularUpload");
  const auto dfgPass = builder.declarePass("EnvironmentDfgUpload");
  builder.writes(environmentPass, environmentResource, atlantis::rhi::ResourceState::TransferDestination);
  builder.writes(dfgPass, dfgResource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(environmentPass, [&candidate](atlantis::rhi::CommandList& cmd) {
    cmd.copyBufferToTexture(*candidate.specularStagingBuffer, *candidate.resources.prefilteredEnvironment,
                            candidate.specularUploadRegions);
  });
  builder.setExecute(dfgPass, [&candidate](atlantis::rhi::CommandList& cmd) {
    cmd.copyBufferToTexture(*candidate.dfgStagingBuffer, *candidate.resources.dfgLut);
  });
  auto compiled = builder.compile();
  ATLANTIS_CHECK(compiled.isOk());
  atlantis::render_graph::execute(
      compiled.value(),
      {{.resource = compiled.value().resourceAt(0),
        .sampledTexture = candidate.resources.prefilteredEnvironment.get(),
        .finalState = atlantis::rhi::ResourceState::ShaderRead},
       {.resource = compiled.value().resourceAt(1),
        .sampledTexture = candidate.resources.dfgLut.get(),
        .finalState = atlantis::rhi::ResourceState::ShaderRead}},
      commandList);
}

void writeEnvironmentIrradianceSh(
    std::span<float, atlantis::asset_system::kEnvironmentShCoefficientCount *
                         atlantis::asset_system::kEnvironmentShComponentCount>
        destination,
    const std::array<float, atlantis::asset_system::kEnvironmentShCoefficientCount *
                                atlantis::asset_system::kEnvironmentShComponentCount>* source) {
  if (source == nullptr) {
    std::fill(destination.begin(), destination.end(), 0.0F);
  } else {
    std::copy(source->begin(), source->end(), destination.begin());
  }
}

}  // namespace atlantis::runtime

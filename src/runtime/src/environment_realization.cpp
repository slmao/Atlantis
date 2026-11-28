#include <atlantis/profile.h>
#include <atlantis/runtime/environment_realization.h>

#include <atlantis/asset_system/environment_artifact.h>
#include <atlantis/asset_system/load_environment.h>
#include <atlantis/log.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace atlantis::runtime {

std::optional<SceneEnvironmentSource> selectSceneEnvironment(
    const asset_system::AssetCatalog& catalog, const asset_system::AssetGuid& asset) {
  const auto* record = catalog.find(asset);
  if (!record || record->type != asset_system::CatalogAssetType::Environment ||
      record->artifactSchema != asset_system::kEnvironmentArtifactSchemaVersion) return std::nullopt;
  return SceneEnvironmentSource{asset, record->artifact, record->metadata};
}

std::optional<asset_system::EnvironmentAssetData> loadSceneEnvironment(
    const SceneEnvironmentSource& selection, const std::filesystem::path& catalogPath) {
  namespace fs = std::filesystem;
  fs::path artifact = selection.artifact;
  fs::path metadata = selection.metadata;
  if (selection.asset) {
    std::error_code ec;
    const auto catalog = fs::absolute(catalogPath, ec);
    if (ec) return std::nullopt;
    const auto root = fs::canonical(catalog.parent_path(), ec);
    if (ec) return std::nullopt;
    const auto contained = [&root](fs::path& file) {
      std::error_code error;
      file = fs::canonical(file, error);
      if (error || !fs::is_regular_file(file, error) || error) return false;
      const auto relative = file.lexically_relative(root);
      return !relative.empty() && *relative.begin() != ".." && relative != ".";
    };
    if (!contained(artifact) || !contained(metadata)) {
      ATLANTIS_LOG_ERROR("scene environment {} has missing or out-of-catalog paths",
                         asset_system::toString(*selection.asset));
      return std::nullopt;
    }
  }
  auto data = selection.asset
      ? asset_system::loadPreviewStudioEnvironment(artifact, metadata, selection.asset)
      : asset_system::loadEnvironmentAsset(artifact, metadata);
  if (data.isErr()) {
    ATLANTIS_LOG_ERROR("scene environment {} artifact or metadata failed validation (code {})",
        selection.asset ? asset_system::toString(*selection.asset) : "startup default", static_cast<int>(data.error()));
    return std::nullopt;
  }
  return std::move(data.value());
}

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
       .mipLevelCount = environmentData.mipCount,
       .debugName = ATL_DEBUG_NAME("environment_prefiltered")});
  if (environmentResult.isErr()) {
    return ResultT::Err(EnvironmentRealizationError::PrefilteredTextureCreateFailed);
  }
  candidate.resources.prefilteredEnvironment = std::move(environmentResult.value());

  auto dfgResult = device.createSampledTexture(
      {.extent = {environmentData.dfgWidth, environmentData.dfgHeight},
       .format = atlantis::rhi::SampledTextureFormat::Rg16Float,
       .debugName = ATL_DEBUG_NAME("environment_dfg_lut")});
  if (dfgResult.isErr()) return ResultT::Err(EnvironmentRealizationError::DfgTextureCreateFailed);
  candidate.resources.dfgLut = std::move(dfgResult.value());

  auto environmentSamplerResult = device.createSampler(
      {.filter = atlantis::rhi::Filter::Linear,
       .addressMode = atlantis::rhi::AddressMode::ClampToEdge,
       .mipFilter = atlantis::rhi::MipFilter::Linear,
       .minLod = 0.0F,
       .maxLod = static_cast<float>(environmentData.mipCount - 1),
       .debugName = ATL_DEBUG_NAME("environment_sampler")});
  if (environmentSamplerResult.isErr()) {
    return ResultT::Err(EnvironmentRealizationError::EnvironmentSamplerCreateFailed);
  }
  candidate.resources.environmentSampler = std::move(environmentSamplerResult.value());

  auto dfgSamplerResult = device.createSampler({.filter = atlantis::rhi::Filter::Linear,
                                                .addressMode = atlantis::rhi::AddressMode::ClampToEdge,
                                                .debugName = ATL_DEBUG_NAME("environment_dfg_sampler")});
  if (dfgSamplerResult.isErr()) return ResultT::Err(EnvironmentRealizationError::DfgSamplerCreateFailed);
  candidate.resources.dfgSampler = std::move(dfgSamplerResult.value());

  const std::size_t specularBytes = environmentData.specularRgba16Float.size() * sizeof(std::uint16_t);
  auto specularStagingResult =
      device.createBuffer({.purpose = atlantis::rhi::BufferPurpose::Staging,
                           .sizeBytes = specularBytes,
                           .debugName = ATL_DEBUG_NAME("environment_specular_staging")});
  if (specularStagingResult.isErr()) {
    return ResultT::Err(EnvironmentRealizationError::SpecularStagingBufferCreateFailed);
  }
  candidate.specularStagingBuffer = std::move(specularStagingResult.value());
  std::memcpy(candidate.specularStagingBuffer->mappedData(), environmentData.specularRgba16Float.data(),
              specularBytes);

  const std::size_t dfgBytes = environmentData.dfgRg16Float.size() * sizeof(std::uint16_t);
  auto dfgStagingResult =
      device.createBuffer({.purpose = atlantis::rhi::BufferPurpose::Staging,
                           .sizeBytes = dfgBytes,
                           .debugName = ATL_DEBUG_NAME("environment_dfg_staging")});
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

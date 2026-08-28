#include <atlantis/runtime/material_realization.h>

#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/material.h>

#include <algorithm>
#include <cstring>

namespace atlantis::runtime {

namespace {

using atlantis::asset_system::MaterialSamplerAddressMode;
using atlantis::asset_system::MaterialSamplerFilter;
using atlantis::asset_system::TextureColorSpace;
using atlantis::renderer::createMaterial;
using atlantis::rhi::AddressMode;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Filter;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SamplerCreateParams;

// Composition-root translation (this module, never Asset System --
// matching Spec 0016/D8's own module-boundary precedent, already
// established by every other Runtime/fixture composition root that
// touches TextureColorSpace).
[[nodiscard]] SampledTextureFormat toSampledTextureFormat(TextureColorSpace colorSpace) {
  switch (colorSpace) {
    case TextureColorSpace::Unorm:
      return SampledTextureFormat::Rgba8Unorm;
    case TextureColorSpace::Srgb:
      return SampledTextureFormat::Rgba8Srgb;
  }
  return SampledTextureFormat::Rgba8Unorm;
}

[[nodiscard]] Filter toFilter(MaterialSamplerFilter filter) {
  switch (filter) {
    case MaterialSamplerFilter::Nearest:
      return Filter::Nearest;
    case MaterialSamplerFilter::Linear:
      return Filter::Linear;
  }
  return Filter::Linear;
}

[[nodiscard]] AddressMode toAddressMode(MaterialSamplerAddressMode addressMode) {
  switch (addressMode) {
    case MaterialSamplerAddressMode::Repeat:
      return AddressMode::Repeat;
    case MaterialSamplerAddressMode::ClampToEdge:
      return AddressMode::ClampToEdge;
  }
  return AddressMode::Repeat;
}

// Mirrors textured_quad_fixture.cpp's own buildTextureUploadPass()
// exactly -- duplicated, not shared, matching that file's own disclosed
// scope note (this module and tests/image_regression/ share no
// existing private-header dependency). Declares SampledTexture as the
// one tracked resource, with only a single TransferDestination usage on
// this pass; the trailing TransferDestination -> ShaderRead transition
// is reached via the caller's own ResourceBinding::finalState, not a
// second usage on this same pass.
void buildTextureUploadPass(atlantis::render_graph::RenderGraphBuilder& builder, atlantis::rhi::Buffer& stagingBuffer,
                             atlantis::rhi::SampledTexture& destination) {
  const auto resource = builder.declareResource("material-texture-upload");
  const auto pass = builder.declarePass("MaterialTextureUpload");
  builder.writes(pass, resource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(pass, [&stagingBuffer, &destination](atlantis::rhi::CommandList& cmd) {
    cmd.copyBufferToTexture(stagingBuffer, destination);
  });
}

}  // namespace

atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError> realizeOneMaterialCandidate(
    atlantis::rhi::Device& device, const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv, atlantis::rhi::Format colorFormat,
    atlantis::asset_system::AssetId materialAssetId, const atlantis::asset_system::MaterialAssetData& materialData,
    const atlantis::asset_system::TextureAssetData& textureData,
    const std::unordered_map<atlantis::asset_system::AssetId, const atlantis::rhi::SampledTexture*>&
        effectiveSampledTextures) {
  using ResultT = atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError>;

  RealizedMaterialCandidate candidate;
  candidate.materialAssetId = materialAssetId;
  candidate.textureAssetId = materialData.textureAsset;

  const atlantis::rhi::SampledTexture* sampledTexturePtr = nullptr;
  const auto existing = effectiveSampledTextures.find(materialData.textureAsset);
  if (existing != effectiveSampledTextures.end()) {
    // D10 dedup: already realized, either persistently or earlier this
    // same frame -- reuse its own real pointer, no new upload.
    sampledTexturePtr = existing->second;
  } else {
    auto textureResult = device.createSampledTexture(SampledTextureCreateParams{
        .extent = Extent2D{textureData.width, textureData.height},
        .format = toSampledTextureFormat(textureData.colorSpace)});
    if (textureResult.isErr()) return ResultT::Err(MaterialRealizationError::SampledTextureCreateFailed);
    candidate.newSampledTexture = std::move(textureResult.value());

    const std::size_t stagingBytes = static_cast<std::size_t>(textureData.width) * textureData.height * 4;
    auto stagingResult =
        device.createBuffer({.purpose = BufferPurpose::Staging, .sizeBytes = stagingBytes});
    if (stagingResult.isErr()) return ResultT::Err(MaterialRealizationError::StagingBufferCreateFailed);
    std::memcpy(stagingResult.value()->mappedData(), textureData.pixelBytes.data(), stagingBytes);
    candidate.stagingBuffer = std::move(stagingResult.value());

    sampledTexturePtr = candidate.newSampledTexture.get();
  }

  auto samplerResult = device.createSampler(
      SamplerCreateParams{.filter = toFilter(materialData.filter), .addressMode = toAddressMode(materialData.addressMode)});
  if (samplerResult.isErr()) return ResultT::Err(MaterialRealizationError::SamplerCreateFailed);
  candidate.sampler = std::move(samplerResult.value());

  auto materialResult = createMaterial(
      device,
      {.vertexShader = {.spirvWords = unlitTexturedVertexSpirv.data(), .wordCount = unlitTexturedVertexSpirv.size()},
       .fragmentShader = {.spirvWords = unlitTexturedFragmentSpirv.data(),
                           .wordCount = unlitTexturedFragmentSpirv.size()},
       .vertexInputLayout = unlitTexturedVertexInputLayout,
       .colorFormat = colorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasSampledTextureBinding = true},
      sampledTexturePtr, candidate.sampler.get());
  if (materialResult.isErr()) return ResultT::Err(MaterialRealizationError::MaterialCreateFailed);
  candidate.material = std::make_unique<atlantis::renderer::Material>(std::move(materialResult.value()));

  return ResultT::Ok(std::move(candidate));
}

std::vector<atlantis::asset_system::AssetId> computePendingMaterialIds(
    const std::vector<atlantis::asset_system::AssetId>& referencedIds,
    const std::vector<atlantis::asset_system::AssetId>& alreadyRealizedIds) {
  std::vector<atlantis::asset_system::AssetId> pending;
  for (atlantis::asset_system::AssetId id : referencedIds) {
    if (std::find(alreadyRealizedIds.begin(), alreadyRealizedIds.end(), id) == alreadyRealizedIds.end()) {
      pending.push_back(id);
    }
  }
  return pending;
}

std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizePendingMaterials(
    atlantis::rhi::Device& device, atlantis::rhi::CommandList& commandList,
    const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv, atlantis::rhi::Format colorFormat,
    const std::vector<atlantis::asset_system::AssetId>& pendingIds,
    const std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>&
        sampledTextureResourceMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>&
        materialDataMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData>&
        textureDataMap) {
  namespace render_graph = atlantis::render_graph;

  // Seeded from the persistent map; extended below with each new
  // candidate's own texture as it succeeds, so a second material
  // realized later in this SAME pendingIds pass that names the same
  // texture also dedups against it, not just against
  // sampledTextureResourceMap_'s own pre-frame state (Human Review
  // Approval item 3).
  std::unordered_map<atlantis::asset_system::AssetId, const atlantis::rhi::SampledTexture*> effectiveSampledTextures;
  for (const auto& [id, texture] : sampledTextureResourceMap) effectiveSampledTextures.emplace(id, texture.get());

  std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realized;
  render_graph::RenderGraphBuilder uploadBuilder;
  // Tracks, in pendingIds' own deterministic order, which ids got a NEW
  // upload pass this call -- the exact order resourceAt() below must be
  // indexed in, since realized (an unordered_map) does not preserve it.
  std::vector<atlantis::asset_system::AssetId> uploadedIds;

  for (atlantis::asset_system::AssetId id : pendingIds) {
    const auto materialIt = materialDataMap.find(id);
    ATLANTIS_CHECK_MSG(materialIt != materialDataMap.end(),
                        "realizePendingMaterials(): pendingIds must only ever name AssetIds Phase 1 already loaded "
                        "into materialDataMap");
    const auto textureIt = textureDataMap.find(materialIt->second.textureAsset);
    ATLANTIS_CHECK_MSG(textureIt != textureDataMap.end(),
                        "realizePendingMaterials(): a material's own textureAsset must already be loaded into "
                        "textureDataMap by Phase 1");

    auto candidateResult = realizeOneMaterialCandidate(device, unlitTexturedVertexInputLayout,
                                                        unlitTexturedVertexSpirv, unlitTexturedFragmentSpirv,
                                                        colorFormat, id, materialIt->second, textureIt->second,
                                                        effectiveSampledTextures);
    if (candidateResult.isErr()) {
      ATLANTIS_LOG_ERROR("realizeOneMaterialCandidate() failed -- material stays pending, retried next frame");
      continue;
    }

    RealizedMaterialCandidate candidate = std::move(candidateResult.value());
    if (candidate.newSampledTexture) {
      buildTextureUploadPass(uploadBuilder, **candidate.stagingBuffer, *candidate.newSampledTexture);
      effectiveSampledTextures.emplace(candidate.textureAssetId, candidate.newSampledTexture.get());
      uploadedIds.push_back(id);
    }
    realized.emplace(id, std::move(candidate));
  }

  if (!uploadedIds.empty()) {
    auto compileResult = uploadBuilder.compile();
    ATLANTIS_CHECK_MSG(compileResult.isOk(), "realizePendingMaterials(): the upload-only RenderGraph never fails to "
                                              "compile (one TransferDestination write per pass, no cross-pass "
                                              "dependency)");
    std::vector<render_graph::ResourceBinding> bindings;
    bindings.reserve(uploadedIds.size());
    for (std::size_t i = 0; i < uploadedIds.size(); ++i) {
      RealizedMaterialCandidate& candidate = realized.at(uploadedIds[i]);
      bindings.push_back({.resource = compileResult.value().resourceAt(i),
                           .sampledTexture = candidate.newSampledTexture.get(),
                           .finalState = atlantis::rhi::ResourceState::ShaderRead});
    }
    render_graph::execute(compileResult.value(), bindings, commandList);
  }

  return realized;
}

atlantis::Result<FormatRebuildCandidates, MaterialRealizationError> rebuildMaterialsForFormatChange(
    atlantis::rhi::Device& device, const atlantis::rhi::VertexInputLayout& fallbackVertexInputLayout,
    const std::vector<std::uint32_t>& fallbackVertexSpirv, const std::vector<std::uint32_t>& fallbackFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv, atlantis::rhi::Format newColorFormat,
    const std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>&
        currentMaterials) {
  using ResultT = atlantis::Result<FormatRebuildCandidates, MaterialRealizationError>;

  FormatRebuildCandidates candidates;

  auto fallbackResult = createMaterial(
      device, {.vertexShader = {.spirvWords = fallbackVertexSpirv.data(), .wordCount = fallbackVertexSpirv.size()},
               .fragmentShader = {.spirvWords = fallbackFragmentSpirv.data(),
                                   .wordCount = fallbackFragmentSpirv.size()},
               .vertexInputLayout = fallbackVertexInputLayout,
               .colorFormat = newColorFormat,
               .depthFormat = DepthFormat::D32Sfloat,
               .pushConstantSizeBytes = sizeof(float) * 16});
  if (fallbackResult.isErr()) return ResultT::Err(MaterialRealizationError::MaterialCreateFailed);
  candidates.fallback = std::make_unique<atlantis::renderer::Material>(std::move(fallbackResult.value()));

  for (const auto& [id, oldMaterial] : currentMaterials) {
    auto rebuiltResult = createMaterial(
        device,
        {.vertexShader = {.spirvWords = unlitTexturedVertexSpirv.data(),
                           .wordCount = unlitTexturedVertexSpirv.size()},
         .fragmentShader = {.spirvWords = unlitTexturedFragmentSpirv.data(),
                             .wordCount = unlitTexturedFragmentSpirv.size()},
         .vertexInputLayout = unlitTexturedVertexInputLayout,
         .colorFormat = newColorFormat,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .hasSampledTextureBinding = true},
        oldMaterial->sampledTexture(), oldMaterial->sampler());
    if (rebuiltResult.isErr()) return ResultT::Err(MaterialRealizationError::MaterialCreateFailed);
    candidates.materials.emplace(id, std::make_unique<atlantis::renderer::Material>(std::move(rebuiltResult.value())));
  }

  return ResultT::Ok(std::move(candidates));
}

}  // namespace atlantis::runtime

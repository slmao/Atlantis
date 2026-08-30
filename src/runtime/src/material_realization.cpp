#include <atlantis/runtime/material_realization.h>

#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/material.h>

#include <algorithm>
#include <cstddef>
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

struct ShaderPairRef {
  const atlantis::rhi::VertexInputLayout* vertexInputLayout;
  const std::vector<std::uint32_t>* vertexSpirv;
  const std::vector<std::uint32_t>* fragmentSpirv;
};

// Plan 0019 Section P6: the one, single Runtime-private dispatch point
// selecting a MaterialKind's own real, built-in shader pair -- both real
// PipelineCreateParams-constructing call sites (realizeOneMaterialCandidate(),
// rebuildMaterialsForFormatChange()'s own per-candidate loop) call this,
// never their own separate switch. No `default:` label -- MaterialKind
// gaining a third enumerator without a matching case here is a build-time
// C4062 error, not a silent fallback (this target already carries
// /w14062, CMakeLists.txt). The ATLANTIS_CHECK_MSG(false, ...) after the
// switch is a genuinely unreachable, fail-fast guard (never a silent
// default value) -- reached only if a future MaterialKind enumerator is
// added AND its own C4062-flagged missing case is force-suppressed,
// which this codebase's own /WX build configuration does not permit to
// happen silently.
[[nodiscard]] ShaderPairRef selectShaderPair(
    atlantis::asset_system::MaterialKind kind, const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv) {
  switch (kind) {
    case atlantis::asset_system::MaterialKind::UnlitTextured:
      return {&unlitTexturedVertexInputLayout, &unlitTexturedVertexSpirv, &unlitTexturedFragmentSpirv};
    case atlantis::asset_system::MaterialKind::LitTextured:
      return {&litTexturedVertexInputLayout, &litTexturedVertexSpirv, &litTexturedFragmentSpirv};
    case atlantis::asset_system::MaterialKind::PbrDirectLit:
      // Plan 0023 Milestone 5: replaces the Milestone 1 bootstrap
      // placeholder now that this function's own signature carries the
      // real PBR shader triple.
      return {&pbrDirectLitVertexInputLayout, &pbrDirectLitVertexSpirv, &pbrDirectLitFragmentSpirv};
  }
  ATLANTIS_CHECK_MSG(false, "selectShaderPair(): unreachable -- MaterialKind's own closed switch above is exhaustive");
  return {&unlitTexturedVertexInputLayout, &unlitTexturedVertexSpirv, &unlitTexturedFragmentSpirv};  // never reached
}

// Plan 0023 Milestone 5 (Spec 0023 D9's own Accepted Correction): the
// second, Renderer-facing closed-switch dispatch this Milestone adds --
// unlike selectShaderPair() above (which selects a Pipeline's own
// shader source), this one selects which push-constant PAYLOAD SHAPE a
// given MaterialKind's own Material expects, consumed by createMaterial()
// call sites below. No `default:` label, same C4062 protection as
// selectShaderPair().
[[nodiscard]] atlantis::renderer::MaterialPushConstantLayout pushConstantLayoutFor(
    atlantis::asset_system::MaterialKind kind) {
  switch (kind) {
    case atlantis::asset_system::MaterialKind::UnlitTextured:
    case atlantis::asset_system::MaterialKind::LitTextured:
      return atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly;
    case atlantis::asset_system::MaterialKind::PbrDirectLit:
      return atlantis::renderer::MaterialPushConstantLayout::PbrDirectLit;
  }
  ATLANTIS_CHECK_MSG(false, "pushConstantLayoutFor(): unreachable -- MaterialKind's own closed switch above is exhaustive");
  return atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly;  // never reached
}

[[nodiscard]] std::size_t pushConstantSizeBytesFor(atlantis::asset_system::MaterialKind kind) {
  switch (kind) {
    case atlantis::asset_system::MaterialKind::UnlitTextured:
    case atlantis::asset_system::MaterialKind::LitTextured:
      return sizeof(float) * 16;
    case atlantis::asset_system::MaterialKind::PbrDirectLit:
      return 96;
  }
  ATLANTIS_CHECK_MSG(false,
                      "pushConstantSizeBytesFor(): unreachable -- MaterialKind's own closed switch above is exhaustive");
  return sizeof(float) * 16;  // never reached
}

}  // namespace

atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError> realizeOneMaterialCandidate(
    atlantis::rhi::Device& device, const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv, atlantis::rhi::Format colorFormat,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv, atlantis::asset_system::AssetId materialAssetId,
    const atlantis::asset_system::MaterialAssetData& materialData,
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

  const ShaderPairRef shaderPair =
      selectShaderPair(materialData.kind, unlitTexturedVertexInputLayout, unlitTexturedVertexSpirv,
                        unlitTexturedFragmentSpirv, litTexturedVertexInputLayout, litTexturedVertexSpirv,
                        litTexturedFragmentSpirv, pbrDirectLitVertexInputLayout, pbrDirectLitVertexSpirv,
                        pbrDirectLitFragmentSpirv);
  // Plan 0023 Milestone 5: pushConstantSizeBytes/pushConstantLayout are
  // 96/PbrDirectLit only for that kind (every other kind keeps today's
  // 64/ObjectToWorldOnly, unchanged); materialData's three PBR fields
  // (defaulted to {1,1,1,1}/1.0f/1.0f for every non-PBR kind since
  // Milestone 1) are forwarded to createMaterial() unconditionally --
  // harmless for a kind whose own Renderer-side switch (renderer.cpp)
  // never reads them.
  auto materialResult = createMaterial(
      device,
      {.vertexShader = {.spirvWords = shaderPair.vertexSpirv->data(), .wordCount = shaderPair.vertexSpirv->size()},
       .fragmentShader = {.spirvWords = shaderPair.fragmentSpirv->data(),
                           .wordCount = shaderPair.fragmentSpirv->size()},
       .vertexInputLayout = *shaderPair.vertexInputLayout,
       .colorFormat = colorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = pushConstantSizeBytesFor(materialData.kind),
       .hasSampledTextureBinding = true},
      sampledTexturePtr, candidate.sampler.get(), pushConstantLayoutFor(materialData.kind),
      {materialData.baseColorFactor[0], materialData.baseColorFactor[1], materialData.baseColorFactor[2],
       materialData.baseColorFactor[3]},
      materialData.metallicFactor, materialData.roughnessFactor);
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
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
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

    auto candidateResult = realizeOneMaterialCandidate(
        device, unlitTexturedVertexInputLayout, unlitTexturedVertexSpirv, unlitTexturedFragmentSpirv, colorFormat,
        litTexturedVertexInputLayout, litTexturedVertexSpirv, litTexturedFragmentSpirv, pbrDirectLitVertexInputLayout,
        pbrDirectLitVertexSpirv, pbrDirectLitFragmentSpirv, id, materialIt->second, textureIt->second,
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
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv, atlantis::rhi::Format newColorFormat,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>&
        materialDataMap,
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
    const auto materialDataIt = materialDataMap.find(id);
    ATLANTIS_CHECK_MSG(materialDataIt != materialDataMap.end(),
                        "rebuildMaterialsForFormatChange(): every id in currentMaterials must already have a "
                        "matching entry in materialDataMap -- both are keyed by the same, already-realized "
                        "material AssetId set");
    const ShaderPairRef shaderPair =
        selectShaderPair(materialDataIt->second.kind, unlitTexturedVertexInputLayout, unlitTexturedVertexSpirv,
                          unlitTexturedFragmentSpirv, litTexturedVertexInputLayout, litTexturedVertexSpirv,
                          litTexturedFragmentSpirv, pbrDirectLitVertexInputLayout, pbrDirectLitVertexSpirv,
                          pbrDirectLitFragmentSpirv);
    // Plan 0023 Milestone 5: matches realizeOneMaterialCandidate()'s own
    // identical pushConstantSizeBytes/pushConstantLayout/PBR-field
    // forwarding, keyed off this entry's own real MaterialKind
    // (materialDataIt->second), never inferred from the OLD Material
    // being replaced.
    auto rebuiltResult = createMaterial(
        device,
        {.vertexShader = {.spirvWords = shaderPair.vertexSpirv->data(),
                           .wordCount = shaderPair.vertexSpirv->size()},
         .fragmentShader = {.spirvWords = shaderPair.fragmentSpirv->data(),
                             .wordCount = shaderPair.fragmentSpirv->size()},
         .vertexInputLayout = *shaderPair.vertexInputLayout,
         .colorFormat = newColorFormat,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = pushConstantSizeBytesFor(materialDataIt->second.kind),
         .hasSampledTextureBinding = true},
        oldMaterial->sampledTexture(), oldMaterial->sampler(), pushConstantLayoutFor(materialDataIt->second.kind),
        {materialDataIt->second.baseColorFactor[0], materialDataIt->second.baseColorFactor[1],
         materialDataIt->second.baseColorFactor[2], materialDataIt->second.baseColorFactor[3]},
        materialDataIt->second.metallicFactor, materialDataIt->second.roughnessFactor);
    if (rebuiltResult.isErr()) return ResultT::Err(MaterialRealizationError::MaterialCreateFailed);
    candidates.materials.emplace(id, std::make_unique<atlantis::renderer::Material>(std::move(rebuiltResult.value())));
  }

  return ResultT::Ok(std::move(candidates));
}

}  // namespace atlantis::runtime

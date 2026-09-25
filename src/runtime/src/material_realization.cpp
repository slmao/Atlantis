#include <atlantis/runtime/material_realization.h>

#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/material.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace atlantis::runtime {

namespace {

using atlantis::asset_system::MaterialSamplerAddressMode;
using atlantis::asset_system::MaterialSamplerFilter;
using atlantis::asset_system::TextureColorSpace;
using atlantis::asset_system::TextureDataLayout;
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
// Spec 0038: the layout dimension joins the color-space dimension --
// still a pure, total function over the pair, still never an Asset
// System concern (the same composition-root translation point).
[[nodiscard]] SampledTextureFormat toSampledTextureFormat(TextureColorSpace colorSpace,
                                                           TextureDataLayout layout) {
  const bool srgb = colorSpace == TextureColorSpace::Srgb;
  switch (layout) {
    case TextureDataLayout::Rgba8:
      return srgb ? SampledTextureFormat::Rgba8Srgb : SampledTextureFormat::Rgba8Unorm;
    case TextureDataLayout::Bc7:
      return srgb ? SampledTextureFormat::Bc7Srgb : SampledTextureFormat::Bc7Unorm;
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

// Spec 0045 (ADR-0093 Decision 3): one upload region per mip level of
// data, at the offsets asset_system::textureMipLevels() -- the single
// layout authority -- gives for the chain in pixelBytes.
[[nodiscard]] std::vector<atlantis::rhi::SampledTextureUploadRegion> textureUploadRegions(
    const atlantis::asset_system::TextureAssetData& data) {
  std::vector<atlantis::rhi::SampledTextureUploadRegion> regions;
  for (const auto& level : atlantis::asset_system::textureMipLevels(data.width, data.height, data.layout,
                                                                     data.mipCount)) {
    regions.push_back({.bufferOffsetBytes = static_cast<std::size_t>(level.offsetBytes),
                       .mipLevel = static_cast<std::uint32_t>(regions.size()),
                       .extent = Extent2D{level.width, level.height}});
  }
  return regions;
}

// Mirrors textured_quad_fixture.cpp's own buildTextureUploadPass()
// exactly -- duplicated, not shared, matching that file's own disclosed
// scope note (this module and tests/image_regression/ share no
// existing private-header dependency). Declares SampledTexture as the
// one tracked resource, with only a single TransferDestination usage on
// this pass; the trailing TransferDestination -> ShaderRead transition
// is reached via the caller's own ResourceBinding::finalState, not a
// second usage on this same pass.
// Spec 0045: every mip level in one copy -- regions borrows the
// candidate's own region vector, alive until the graph executes.
void buildTextureUploadPass(atlantis::render_graph::RenderGraphBuilder& builder, atlantis::rhi::Buffer& stagingBuffer,
                             atlantis::rhi::SampledTexture& destination,
                             std::span<const atlantis::rhi::SampledTextureUploadRegion> regions) {
  const auto resource = builder.declareResource("material-texture-upload");
  const auto pass = builder.declarePass("MaterialTextureUpload");
  builder.writes(pass, resource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(pass, [&stagingBuffer, &destination, regions](atlantis::rhi::CommandList& cmd) {
    cmd.copyBufferToTexture(stagingBuffer, destination, regions);
  });
}

struct ShaderPairRef {
  const atlantis::rhi::VertexInputLayout* vertexInputLayout;
  const std::vector<std::uint32_t>* vertexSpirv;
  const std::vector<std::uint32_t>* fragmentSpirv;
};

// Plan 0019 Section P6: the one, single Runtime-private dispatch point
// selecting a MaterialKind's own real, built-in shader pair --
// realizeOneMaterialCandidate() calls this, never its own separate
// switch. No `default:` label -- MaterialKind
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
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitNormalMapFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own two IBL-lit
    // trios -- see this file's own realizeOneMaterialCandidate() header
    // comment for the full parameter-list rationale.
    const atlantis::rhi::VertexInputLayout& pbrClearcoatIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrClearcoatIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrClearcoatIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrClearcoatIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrClearcoatIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrClearcoatIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own two IBL-lit
    // trios, same insertion point/shape as the PbrClearcoat pair above.
    const atlantis::rhi::VertexInputLayout& pbrSheenIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrSheenIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrSheenIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrSheenIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrSheenIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrSheenIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own two IBL-lit
    // trios, same insertion point/shape as the PbrClearcoat/PbrSheen
    // pairs above. Both variants (not just NormalMap) need the tangent
    // vertex attribute -- their vertexInputLayout is always
    // pbrNormalMapVertexLayout()-shaped, never pbrDirectLitVertexLayout()
    // -shaped, so both trios below are threaded from the caller's own
    // *NormalMapVertexInputLayout binding (see runtime_application.cpp).
    const atlantis::rhi::VertexInputLayout& pbrAnisotropicIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrAnisotropicIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrAnisotropicIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrAnisotropicIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrAnisotropicIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrAnisotropicIblNormalMapFragmentSpirv, bool environmentEnabled,
    bool hasNormalMap) {
  switch (kind) {
    case atlantis::asset_system::MaterialKind::UnlitTextured:
      return {&unlitTexturedVertexInputLayout, &unlitTexturedVertexSpirv, &unlitTexturedFragmentSpirv};
    case atlantis::asset_system::MaterialKind::LitTextured:
      return {&litTexturedVertexInputLayout, &litTexturedVertexSpirv, &litTexturedFragmentSpirv};
    case atlantis::asset_system::MaterialKind::PbrDirectLit:
      // Plan 0023 Milestone 5: replaces the Milestone 1 bootstrap
      // placeholder now that this function's own signature carries the
      // real PBR shader triple.
      // Plan 0029 Section P15 (ADR-0074): hasNormalMap selects between
      // the normal-map and plain PBR trio, orthogonally to
      // environmentEnabled's own None/Ibl selection.
      if (hasNormalMap) {
        if (environmentEnabled) {
          return {&pbrIblNormalMapVertexInputLayout, &pbrIblNormalMapVertexSpirv, &pbrIblNormalMapFragmentSpirv};
        }
        return {&pbrDirectLitNormalMapVertexInputLayout, &pbrDirectLitNormalMapVertexSpirv,
                &pbrDirectLitNormalMapFragmentSpirv};
      }
      if (environmentEnabled) return {&pbrIblVertexInputLayout, &pbrIblVertexSpirv, &pbrIblFragmentSpirv};
      return {&pbrDirectLitVertexInputLayout, &pbrDirectLitVertexSpirv, &pbrDirectLitFragmentSpirv};
    case atlantis::asset_system::MaterialKind::PbrClearcoat:
      // Plan 0035 Milestone 2 (ADR-0081/Spec 0035 Requirement 4):
      // IBL-only this round -- no direct-lit clearcoat shader exists,
      // so a PbrClearcoat material realized without an environment is a
      // real, disclosed configuration error, not silently handled by
      // falling back to some other kind's shader.
      ATLANTIS_CHECK_MSG(environmentEnabled,
                          "selectShaderPair(): MaterialKind::PbrClearcoat requires an environment this round "
                          "(Spec 0035's own IBL-only scope) -- no direct-lit clearcoat shader exists yet");
      if (hasNormalMap) {
        return {&pbrClearcoatIblNormalMapVertexInputLayout, &pbrClearcoatIblNormalMapVertexSpirv,
                &pbrClearcoatIblNormalMapFragmentSpirv};
      }
      return {&pbrClearcoatIblVertexInputLayout, &pbrClearcoatIblVertexSpirv, &pbrClearcoatIblFragmentSpirv};
    case atlantis::asset_system::MaterialKind::PbrSheen:
      // Plan 0035 Milestone 3 (ADR-0081/Spec 0035 Requirement 4):
      // IBL-only this round, mirroring PbrClearcoat's own identical gate
      // immediately above -- no direct-lit sheen shader exists yet.
      ATLANTIS_CHECK_MSG(environmentEnabled,
                          "selectShaderPair(): MaterialKind::PbrSheen requires an environment this round (Spec "
                          "0035's own IBL-only scope) -- no direct-lit sheen shader exists yet");
      if (hasNormalMap) {
        return {&pbrSheenIblNormalMapVertexInputLayout, &pbrSheenIblNormalMapVertexSpirv,
                &pbrSheenIblNormalMapFragmentSpirv};
      }
      return {&pbrSheenIblVertexInputLayout, &pbrSheenIblVertexSpirv, &pbrSheenIblFragmentSpirv};
    case atlantis::asset_system::MaterialKind::PbrAnisotropic:
      // Plan 0035 Milestone 4 (ADR-0081/Spec 0035 Requirement 4):
      // IBL-only this round, mirroring PbrClearcoat/PbrSheen's own
      // identical gate above -- no direct-lit anisotropic shader exists
      // yet.
      ATLANTIS_CHECK_MSG(environmentEnabled,
                          "selectShaderPair(): MaterialKind::PbrAnisotropic requires an environment this round "
                          "(Spec 0035's own IBL-only scope) -- no direct-lit anisotropic shader exists yet");
      if (hasNormalMap) {
        return {&pbrAnisotropicIblNormalMapVertexInputLayout, &pbrAnisotropicIblNormalMapVertexSpirv,
                &pbrAnisotropicIblNormalMapFragmentSpirv};
      }
      return {&pbrAnisotropicIblVertexInputLayout, &pbrAnisotropicIblVertexSpirv, &pbrAnisotropicIblFragmentSpirv};
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
    case atlantis::asset_system::MaterialKind::PbrClearcoat:
      return atlantis::renderer::MaterialPushConstantLayout::PbrClearcoat;
    case atlantis::asset_system::MaterialKind::PbrSheen:
      return atlantis::renderer::MaterialPushConstantLayout::PbrSheen;
    case atlantis::asset_system::MaterialKind::PbrAnisotropic:
      return atlantis::renderer::MaterialPushConstantLayout::PbrAnisotropic;
  }
  ATLANTIS_CHECK_MSG(false, "pushConstantLayoutFor(): unreachable -- MaterialKind's own closed switch above is exhaustive");
  return atlantis::renderer::MaterialPushConstantLayout::ObjectToWorldOnly;  // never reached
}

[[nodiscard]] std::size_t pushConstantSizeBytesFor(atlantis::asset_system::MaterialKind kind) {
  switch (kind) {
    case atlantis::asset_system::MaterialKind::UnlitTextured:
    case atlantis::asset_system::MaterialKind::LitTextured:
      return sizeof(float) * 16;
    // Plan 0041 Milestone 2 (Spec 0041 R5): every PBR size below grew by 16
    // with emissiveFactor (PbrSheen reaching the 128-byte guarantee
    // exactly). One of three hand-kept copies -- see
    // compile_and_validate.cpp's own note; Runtime may not include a
    // Renderer private header.
    case atlantis::asset_system::MaterialKind::PbrDirectLit:
      return 112;
    // Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own independent
    // 96-byte layout (PbrClearcoatPushConstants,
    // src/renderer/src/pbr_clearcoat_push_constants.h) -- numerically
    // identical to PbrDirectLit's own 96 but a DIFFERENT struct shape
    // (objectToWorld/baseColorFactor/metallicFactor/roughnessFactor/
    // clearcoatFactor/clearcoatRoughness, no padding needed -- the
    // struct's own static_asserts confirm this), not a coincidence to
    // collapse into one case label.
    case atlantis::asset_system::MaterialKind::PbrClearcoat:
      return 112;
    // Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own independent
    // 112-byte layout (PbrSheenPushConstants,
    // src/renderer/src/pbr_sheen_push_constants.h) -- NOT 96, unlike
    // PbrClearcoat above; sheenColor's own real vec3 alignment padding
    // (ADR-0081's own flagged "likely tipping point") pushes this
    // struct's own real, measured size wider, confirmed by that
    // struct's own static_asserts and by atlantis_shader_compiler's own
    // real Slang reflection of pbr_sheen_ibl.slang/
    // pbr_sheen_ibl_normal_map.slang (compile_and_validate.cpp), not
    // assumed here.
    case atlantis::asset_system::MaterialKind::PbrSheen:
      return 128;
    // Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own independent
    // 96-byte layout (PbrAnisotropicPushConstants,
    // src/renderer/src/pbr_anisotropic_push_constants.h) -- two plain
    // trailing scalars, no padding, same as PbrClearcoat above (NOT
    // PbrSheen's 112, which vec3 alignment forces wider) -- confirmed
    // by that struct's own static_asserts and by
    // atlantis_shader_compiler's own real Slang reflection of
    // pbr_anisotropic_ibl.slang/pbr_anisotropic_ibl_normal_map.slang
    // (compile_and_validate.cpp), not assumed here.
    case atlantis::asset_system::MaterialKind::PbrAnisotropic:
      return 112;
  }
  ATLANTIS_CHECK_MSG(false,
                      "pushConstantSizeBytesFor(): unreachable -- MaterialKind's own closed switch above is exhaustive");
  return sizeof(float) * 16;  // never reached
}

}  // namespace

atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError> realizeOneMaterialCandidate(
    atlantis::rhi::Device& device, const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitNormalMapFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own two IBL-lit
    // shader trios, inserted immediately after the existing
    // pbrIblNormalMap* trio -- selectShaderPair() (above) picks between
    // them via hasNormalMap, exactly like the PbrDirectLit quartet.
    const atlantis::rhi::VertexInputLayout& pbrClearcoatIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrClearcoatIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrClearcoatIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrClearcoatIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrClearcoatIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrClearcoatIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own two IBL-lit
    // shader trios, inserted immediately after the existing
    // pbrClearcoatIblNormalMap* trio -- selectShaderPair() (above) picks
    // between them via hasNormalMap, exactly like the PbrClearcoat pair.
    const atlantis::rhi::VertexInputLayout& pbrSheenIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrSheenIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrSheenIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrSheenIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrSheenIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrSheenIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own two IBL-lit
    // shader trios, inserted immediately after the existing
    // pbrSheenIblNormalMap* trio -- IBL-only this round (Spec 0035's own
    // scope), no direct-lit anisotropic trio exists or is threaded here.
    const atlantis::rhi::VertexInputLayout& pbrAnisotropicIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrAnisotropicIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrAnisotropicIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrAnisotropicIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrAnisotropicIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrAnisotropicIblNormalMapFragmentSpirv, bool environmentEnabled,
    atlantis::asset_system::AssetId materialAssetId,
    const atlantis::asset_system::MaterialAssetData& materialData,
    const atlantis::asset_system::TextureAssetData& textureData,
    const atlantis::asset_system::TextureAssetData* normalMapTextureData,
    const std::unordered_map<atlantis::asset_system::AssetId, const atlantis::rhi::SampledTexture*>&
        effectiveSampledTextures) {
  using ResultT = atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError>;
  const bool hasNormalMap = materialData.normalMapTexture != 0;

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
        .format = toSampledTextureFormat(textureData.colorSpace, textureData.layout),
        .mipLevelCount = textureData.mipCount});
    if (textureResult.isErr()) return ResultT::Err(MaterialRealizationError::SampledTextureCreateFailed);
    candidate.newSampledTexture = std::move(textureResult.value());

    // Spec 0038: the payload size is layout-derived -- pixelBytes.size()
    // is exactly the upload size for both Rgba8 and Bc7 layouts.
    const std::size_t stagingBytes = textureData.pixelBytes.size();
    auto stagingResult =
        device.createBuffer({.purpose = BufferPurpose::Staging, .sizeBytes = stagingBytes});
    if (stagingResult.isErr()) return ResultT::Err(MaterialRealizationError::StagingBufferCreateFailed);
    std::memcpy(stagingResult.value()->mappedData(), textureData.pixelBytes.data(), stagingBytes);
    candidate.stagingBuffer = std::move(stagingResult.value());
    candidate.uploadRegions = textureUploadRegions(textureData);

    sampledTexturePtr = candidate.newSampledTexture.get();
  }

  // Plan 0029 Section P15 (ADR-0074): the identical dedup-then-create
  // sequence as the base-color texture above, keyed by
  // materialData.normalMapTexture -- skipped entirely (normalMapTexturePtr
  // stays null) when this material declares no normal map.
  const atlantis::rhi::SampledTexture* normalMapTexturePtr = nullptr;
  if (hasNormalMap) {
    ATLANTIS_CHECK_MSG(normalMapTextureData != nullptr,
                        "realizeOneMaterialCandidate(): a material declaring normalMapTexture must be called with "
                        "its own normalMapTextureData already resolved by the caller");
    candidate.normalMapTextureAssetId = materialData.normalMapTexture;
    const auto existingNormalMap = effectiveSampledTextures.find(materialData.normalMapTexture);
    if (existingNormalMap != effectiveSampledTextures.end()) {
      normalMapTexturePtr = existingNormalMap->second;
    } else {
      auto normalMapTextureResult = device.createSampledTexture(SampledTextureCreateParams{
          .extent = Extent2D{normalMapTextureData->width, normalMapTextureData->height},
          .format = toSampledTextureFormat(normalMapTextureData->colorSpace, normalMapTextureData->layout),
          .mipLevelCount = normalMapTextureData->mipCount});
      if (normalMapTextureResult.isErr()) return ResultT::Err(MaterialRealizationError::SampledTextureCreateFailed);
      candidate.newNormalMapTexture = std::move(normalMapTextureResult.value());

      const std::size_t normalMapStagingBytes = normalMapTextureData->pixelBytes.size();
      auto normalMapStagingResult =
          device.createBuffer({.purpose = BufferPurpose::Staging, .sizeBytes = normalMapStagingBytes});
      if (normalMapStagingResult.isErr()) return ResultT::Err(MaterialRealizationError::StagingBufferCreateFailed);
      std::memcpy(normalMapStagingResult.value()->mappedData(), normalMapTextureData->pixelBytes.data(),
                  normalMapStagingBytes);
      candidate.normalMapStagingBuffer = std::move(normalMapStagingResult.value());
      candidate.normalMapUploadRegions = textureUploadRegions(*normalMapTextureData);

      normalMapTexturePtr = candidate.newNormalMapTexture.get();
    }
  }

  // Spec 0045 R5 / ADR-0093 Decision 4, ruling O4: maxLod reaches the
  // deepest level of the textures this sampler serves (the created
  // resources are the authority, deduplicated ones included); mipFilter
  // follows filter only when there is a level to filter towards. A
  // material whose textures are all single-mip gets maxLod 0 and
  // MipFilter::Nearest -- today's sampler, field for field.
  std::uint32_t deepestMipLevel = sampledTexturePtr->mipLevelCount() - 1;
  if (normalMapTexturePtr != nullptr) {
    deepestMipLevel = std::max(deepestMipLevel, normalMapTexturePtr->mipLevelCount() - 1);
  }
  const atlantis::rhi::Filter samplerFilter = toFilter(materialData.filter);
  auto samplerResult = device.createSampler(SamplerCreateParams{
      .filter = samplerFilter,
      .addressMode = toAddressMode(materialData.addressMode),
      .mipFilter = deepestMipLevel > 0 && samplerFilter == atlantis::rhi::Filter::Linear
                       ? atlantis::rhi::MipFilter::Linear
                       : atlantis::rhi::MipFilter::Nearest,
      .maxLod = static_cast<float>(deepestMipLevel)});
  if (samplerResult.isErr()) return ResultT::Err(MaterialRealizationError::SamplerCreateFailed);
  candidate.sampler = std::move(samplerResult.value());

  const ShaderPairRef shaderPair =
      selectShaderPair(materialData.kind, unlitTexturedVertexInputLayout, unlitTexturedVertexSpirv,
                        unlitTexturedFragmentSpirv, litTexturedVertexInputLayout, litTexturedVertexSpirv,
                        litTexturedFragmentSpirv, pbrDirectLitVertexInputLayout, pbrDirectLitVertexSpirv,
                        pbrDirectLitFragmentSpirv, pbrIblVertexInputLayout, pbrIblVertexSpirv, pbrIblFragmentSpirv,
                        pbrDirectLitNormalMapVertexInputLayout, pbrDirectLitNormalMapVertexSpirv,
                        pbrDirectLitNormalMapFragmentSpirv, pbrIblNormalMapVertexInputLayout,
                        pbrIblNormalMapVertexSpirv, pbrIblNormalMapFragmentSpirv, pbrClearcoatIblVertexInputLayout,
                        pbrClearcoatIblVertexSpirv, pbrClearcoatIblFragmentSpirv,
                        pbrClearcoatIblNormalMapVertexInputLayout, pbrClearcoatIblNormalMapVertexSpirv,
                        pbrClearcoatIblNormalMapFragmentSpirv, pbrSheenIblVertexInputLayout, pbrSheenIblVertexSpirv,
                        pbrSheenIblFragmentSpirv, pbrSheenIblNormalMapVertexInputLayout,
                        pbrSheenIblNormalMapVertexSpirv, pbrSheenIblNormalMapFragmentSpirv,
                        pbrAnisotropicIblVertexInputLayout, pbrAnisotropicIblVertexSpirv,
                        pbrAnisotropicIblFragmentSpirv, pbrAnisotropicIblNormalMapVertexInputLayout,
                        pbrAnisotropicIblNormalMapVertexSpirv, pbrAnisotropicIblNormalMapFragmentSpirv,
                        environmentEnabled, hasNormalMap);
  // Plan 0023 Milestone 5: pushConstantSizeBytes/pushConstantLayout are
  // 96/PbrDirectLit only for that kind (every other kind keeps today's
  // 64/ObjectToWorldOnly, unchanged); materialData's three PBR fields
  // (defaulted to {1,1,1,1}/1.0f/1.0f for every non-PBR kind since
  // Milestone 1) are forwarded to createMaterial() unconditionally --
  // harmless for a kind whose own Renderer-side switch (renderer.cpp)
  // never reads them.
  const AlphaModeRealization alphaRealization = alphaModeRealizationFor(materialData.alphaMode);
  auto materialResult = createMaterial(
      device,
      {.vertexShader = {.spirvWords = shaderPair.vertexSpirv->data(), .wordCount = shaderPair.vertexSpirv->size()},
       .fragmentShader = {.spirvWords = shaderPair.fragmentSpirv->data(),
                           .wordCount = shaderPair.fragmentSpirv->size()},
       .vertexInputLayout = *shaderPair.vertexInputLayout,
       // Plan 0024 Milestone 6 (ADR-0068 D-1/D-3): every geometry
       // Pipeline now renders into the fixed HDR intermediate, never
       // the caller's real, final Format -- M1's retyped colorFormat
       // (std::variant<Format, HdrFormat>) accepts this directly.
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = pushConstantSizeBytesFor(materialData.kind),
       .sampledTextureBindingCount =
           sampledTextureBindingCountFor(materialData.kind, environmentEnabled, hasNormalMap),
       // Plan 0042 Milestone 1 (ADR-0090 Decisions 1/3).
       .depthWriteEnabled = alphaRealization.depthWriteEnabled,
       .colorBlendMode = alphaRealization.colorBlendMode},
      sampledTexturePtr, candidate.sampler.get(), pushConstantLayoutFor(materialData.kind),
      {materialData.baseColorFactor[0], materialData.baseColorFactor[1], materialData.baseColorFactor[2],
       materialData.baseColorFactor[3]},
      materialData.metallicFactor, materialData.roughnessFactor,
      // Plan 0035 Milestone 2 (ADR-0081), widened by Milestones 3/4:
      // PbrClearcoat, PbrSheen, and PbrAnisotropic are all IBL-only this
      // round (Spec 0035's own scope, enforced above by
      // selectShaderPair()'s own ATLANTIS_CHECK), so all are Ibl-bound
      // exactly like PbrDirectLit's own existing condition -- all four
      // kinds share the identical "environment-bound iff this kind AND
      // environmentEnabled" shape, not collapsed into one shared check
      // since a future direct-lit variant of any would need to diverge
      // here.
      (materialData.kind == atlantis::asset_system::MaterialKind::PbrDirectLit ||
       materialData.kind == atlantis::asset_system::MaterialKind::PbrClearcoat ||
       materialData.kind == atlantis::asset_system::MaterialKind::PbrSheen ||
       materialData.kind == atlantis::asset_system::MaterialKind::PbrAnisotropic) &&
              environmentEnabled
          ? atlantis::renderer::MaterialEnvironmentBinding::Ibl
          : atlantis::renderer::MaterialEnvironmentBinding::None,
      normalMapTexturePtr, materialData.clearcoatFactor, materialData.clearcoatRoughness,
      {materialData.sheenColor[0], materialData.sheenColor[1], materialData.sheenColor[2]},
      materialData.sheenRoughness, materialData.anisotropyFactor, materialData.anisotropyRotation,
      {materialData.emissiveFactor[0], materialData.emissiveFactor[1], materialData.emissiveFactor[2]},
      alphaRealization.rendererAlphaMode,
      // Plan 0042 Milestone 2 (Spec 0042 R6): pushed as 0 unless Mask, so
      // Opaque/Blend never discard.
      materialData.alphaMode == atlantis::asset_system::MaterialAlphaMode::Mask ? materialData.alphaCutoff : 0.0f);
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
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitNormalMapFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 2 (ADR-0081): identical insertion point and
    // threading as realizeOneMaterialCandidate()'s own two new trios.
    const atlantis::rhi::VertexInputLayout& pbrClearcoatIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrClearcoatIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrClearcoatIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrClearcoatIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrClearcoatIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrClearcoatIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 3 (ADR-0081): identical insertion point and
    // threading as realizeOneMaterialCandidate()'s own two new trios.
    const atlantis::rhi::VertexInputLayout& pbrSheenIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrSheenIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrSheenIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrSheenIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrSheenIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrSheenIblNormalMapFragmentSpirv,
    // Plan 0035 Milestone 4 (ADR-0081): identical insertion point and
    // threading as realizeOneMaterialCandidate()'s own two new trios.
    const atlantis::rhi::VertexInputLayout& pbrAnisotropicIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrAnisotropicIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrAnisotropicIblFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrAnisotropicIblNormalMapVertexInputLayout,
    const std::vector<std::uint32_t>& pbrAnisotropicIblNormalMapVertexSpirv,
    const std::vector<std::uint32_t>& pbrAnisotropicIblNormalMapFragmentSpirv, bool environmentEnabled,
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
  // Plan 0029 Section P15: a candidate may now record up to two upload
  // passes (base color and normal map), so upload bookkeeping tracks
  // the uploaded SampledTexture pointers directly, in the exact order
  // their own buildTextureUploadPass() calls below declare them -- the
  // order resourceAt() must be indexed in -- rather than one material
  // AssetId per upload as before.
  std::vector<atlantis::rhi::SampledTexture*> uploadedTextures;

  for (atlantis::asset_system::AssetId id : pendingIds) {
    const auto materialIt = materialDataMap.find(id);
    ATLANTIS_CHECK_MSG(materialIt != materialDataMap.end(),
                        "realizePendingMaterials(): pendingIds must only ever name AssetIds Phase 1 already loaded "
                        "into materialDataMap");
    const auto textureIt = textureDataMap.find(materialIt->second.textureAsset);
    ATLANTIS_CHECK_MSG(textureIt != textureDataMap.end(),
                        "realizePendingMaterials(): a material's own textureAsset must already be loaded into "
                        "textureDataMap by Phase 1");
    const atlantis::asset_system::TextureAssetData* normalMapTextureData = nullptr;
    if (materialIt->second.normalMapTexture != 0) {
      const auto normalMapTextureIt = textureDataMap.find(materialIt->second.normalMapTexture);
      ATLANTIS_CHECK_MSG(normalMapTextureIt != textureDataMap.end(),
                          "realizePendingMaterials(): a material's own normalMapTexture must already be loaded "
                          "into textureDataMap by Phase 1");
      normalMapTextureData = &normalMapTextureIt->second;
    }

    auto candidateResult = realizeOneMaterialCandidate(
        device, unlitTexturedVertexInputLayout, unlitTexturedVertexSpirv, unlitTexturedFragmentSpirv,
        litTexturedVertexInputLayout, litTexturedVertexSpirv, litTexturedFragmentSpirv, pbrDirectLitVertexInputLayout,
        pbrDirectLitVertexSpirv, pbrDirectLitFragmentSpirv, pbrIblVertexInputLayout, pbrIblVertexSpirv,
        pbrIblFragmentSpirv, pbrDirectLitNormalMapVertexInputLayout, pbrDirectLitNormalMapVertexSpirv,
        pbrDirectLitNormalMapFragmentSpirv, pbrIblNormalMapVertexInputLayout, pbrIblNormalMapVertexSpirv,
        pbrIblNormalMapFragmentSpirv, pbrClearcoatIblVertexInputLayout, pbrClearcoatIblVertexSpirv,
        pbrClearcoatIblFragmentSpirv, pbrClearcoatIblNormalMapVertexInputLayout, pbrClearcoatIblNormalMapVertexSpirv,
        pbrClearcoatIblNormalMapFragmentSpirv, pbrSheenIblVertexInputLayout, pbrSheenIblVertexSpirv,
        pbrSheenIblFragmentSpirv, pbrSheenIblNormalMapVertexInputLayout, pbrSheenIblNormalMapVertexSpirv,
        pbrSheenIblNormalMapFragmentSpirv, pbrAnisotropicIblVertexInputLayout, pbrAnisotropicIblVertexSpirv,
        pbrAnisotropicIblFragmentSpirv, pbrAnisotropicIblNormalMapVertexInputLayout,
        pbrAnisotropicIblNormalMapVertexSpirv, pbrAnisotropicIblNormalMapFragmentSpirv, environmentEnabled, id,
        materialIt->second, textureIt->second, normalMapTextureData, effectiveSampledTextures);
    if (candidateResult.isErr()) {
      ATLANTIS_LOG_ERROR("realizeOneMaterialCandidate() failed -- material stays pending, retried next frame");
      continue;
    }

    RealizedMaterialCandidate candidate = std::move(candidateResult.value());
    if (candidate.newSampledTexture) {
      buildTextureUploadPass(uploadBuilder, **candidate.stagingBuffer, *candidate.newSampledTexture,
                             candidate.uploadRegions);
      effectiveSampledTextures.emplace(candidate.textureAssetId, candidate.newSampledTexture.get());
      uploadedTextures.push_back(candidate.newSampledTexture.get());
    }
    if (candidate.newNormalMapTexture) {
      buildTextureUploadPass(uploadBuilder, **candidate.normalMapStagingBuffer, *candidate.newNormalMapTexture,
                             candidate.normalMapUploadRegions);
      effectiveSampledTextures.emplace(candidate.normalMapTextureAssetId, candidate.newNormalMapTexture.get());
      uploadedTextures.push_back(candidate.newNormalMapTexture.get());
    }
    realized.emplace(id, std::move(candidate));
  }

  if (!uploadedTextures.empty()) {
    auto compileResult = uploadBuilder.compile();
    ATLANTIS_CHECK_MSG(compileResult.isOk(), "realizePendingMaterials(): the upload-only RenderGraph never fails to "
                                              "compile (one TransferDestination write per pass, no cross-pass "
                                              "dependency)");
    std::vector<render_graph::ResourceBinding> bindings;
    bindings.reserve(uploadedTextures.size());
    for (std::size_t i = 0; i < uploadedTextures.size(); ++i) {
      bindings.push_back({.resource = compileResult.value().resourceAt(i),
                           .sampledTexture = uploadedTextures[i],
                           .finalState = atlantis::rhi::ResourceState::ShaderRead});
    }
    render_graph::execute(compileResult.value(), bindings, commandList);
  }

  return realized;
}

bool isSrgbFormat(atlantis::rhi::Format format) {
  switch (format) {
    case atlantis::rhi::Format::Unknown:
    case atlantis::rhi::Format::Bgra8Unorm:
    case atlantis::rhi::Format::Rgba8Unorm:
      return false;
    case atlantis::rhi::Format::Bgra8Srgb:
    case atlantis::rhi::Format::Rgba8Srgb:
      return true;
  }
  ATLANTIS_CHECK_MSG(false, "isSrgbFormat(): unreachable -- Format's own closed switch above is exhaustive");
  return false;  // never reached
}

AlphaModeRealization alphaModeRealizationFor(atlantis::asset_system::MaterialAlphaMode alphaMode) {
  switch (alphaMode) {
    case atlantis::asset_system::MaterialAlphaMode::Opaque:
      return {.colorBlendMode = atlantis::rhi::ColorBlendMode::Disabled,
              .depthWriteEnabled = true,
              .rendererAlphaMode = atlantis::renderer::MaterialAlphaMode::Opaque};
    case atlantis::asset_system::MaterialAlphaMode::Mask:
      return {.colorBlendMode = atlantis::rhi::ColorBlendMode::Disabled,
              .depthWriteEnabled = true,
              .rendererAlphaMode = atlantis::renderer::MaterialAlphaMode::Mask};
    case atlantis::asset_system::MaterialAlphaMode::Blend:
      return {.colorBlendMode = atlantis::rhi::ColorBlendMode::AlphaBlend,
              .depthWriteEnabled = false,
              .rendererAlphaMode = atlantis::renderer::MaterialAlphaMode::Blend};
  }
  ATLANTIS_CHECK_MSG(false, "alphaModeRealizationFor(): unreachable -- MaterialAlphaMode's closed switch is exhaustive");
  return {};
}

std::uint32_t sampledTextureBindingCountFor(atlantis::asset_system::MaterialKind kind, bool environmentEnabled,
                                             bool hasNormalMap) {
  switch (kind) {
    case atlantis::asset_system::MaterialKind::UnlitTextured:
    case atlantis::asset_system::MaterialKind::LitTextured:
      return 1U;
    case atlantis::asset_system::MaterialKind::PbrDirectLit:
      if (hasNormalMap) return environmentEnabled ? 5U : 3U;
      return environmentEnabled ? 4U : 2U;
    // Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own binding
    // layout has no shadow-map slot (this Milestone's own disclosed
    // IBL-only, no-shadow scope) -- base-color@1, environment
    // cubemap@2, DFG LUT@3 (+normal map@4 when hasNormalMap) --
    // pbr_clearcoat_ibl.slang/pbr_clearcoat_ibl_normal_map.slang both
    // declare exactly that many bindings. environmentEnabled is
    // asserted true elsewhere (selectShaderPair()) for this kind; the
    // false arms here exist only so this function itself stays a total
    // function over its own parameter domain, never called in that
    // configuration in practice.
    case atlantis::asset_system::MaterialKind::PbrClearcoat:
      if (hasNormalMap) return environmentEnabled ? 4U : 3U;
      return environmentEnabled ? 3U : 2U;
    // Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own binding layout is
    // identical in shape to PbrClearcoat's own immediately above --
    // pbr_sheen_ibl.slang/pbr_sheen_ibl_normal_map.slang declare the
    // same base-color@1/environment@2/DFG-LUT@3(/normal-map@4) bindings.
    case atlantis::asset_system::MaterialKind::PbrSheen:
      if (hasNormalMap) return environmentEnabled ? 4U : 3U;
      return environmentEnabled ? 3U : 2U;
    // Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own binding
    // layout is identical in shape to PbrClearcoat/PbrSheen's own
    // immediately above -- pbr_anisotropic_ibl.slang/
    // pbr_anisotropic_ibl_normal_map.slang declare the same base-
    // color@1/environment@2/DFG-LUT@3(/normal-map@4) bindings. The
    // extra tangent vertex attribute both variants need is orthogonal
    // to this descriptor-binding count -- no new binding for it.
    case atlantis::asset_system::MaterialKind::PbrAnisotropic:
      if (hasNormalMap) return environmentEnabled ? 4U : 3U;
      return environmentEnabled ? 3U : 2U;
  }
  ATLANTIS_CHECK_MSG(
      false, "sampledTextureBindingCountFor(): unreachable -- MaterialKind's own closed switch above is exhaustive");
  return 1U;  // never reached
}

// Plan 0024 Milestone 6 (correction, ADR-0068 D-4): rebuildMaterialsForFormatChange()
// and FormatRebuildCandidates are retired here -- see material_realization.h's
// own comment at this exact point for the full reasoning. Every
// geometry Pipeline now targets the fixed HdrFormat::Rgba16Float
// unconditionally (realizeOneMaterialCandidate() above,
// runtime_application.cpp's own fallbackMaterial_ startup creation),
// so no format-triggered Material rebuild exists anywhere in this
// module any longer.

}  // namespace atlantis::runtime

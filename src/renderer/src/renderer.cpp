#include <atlantis/renderer/bloom.h>
#include <atlantis/renderer/renderer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/renderer/draw_order.h>

#include "exposure.h"
#include "pbr_anisotropic_push_constants.h"
#include "pbr_clearcoat_push_constants.h"
#include "pbr_push_constants.h"
#include "pbr_sheen_push_constants.h"

namespace atlantis::renderer {

namespace {

// Fixed, Renderer-internal constant (not a caller-configurable parameter
// this round -- consistent with Spec 0007's own minimal-material scope; a
// future spec may expose it).
constexpr atlantis::rhi::ClearColorValue kBackgroundClearColor{0.05f, 0.05f, 0.08f, 1.0f};

[[nodiscard]] bool isBlended(const DrawItem& item) { return item.material->alphaMode() == MaterialAlphaMode::Blend; }

// objectToWorld is column-major (translation at [12..14]), the layout
// every caller writes it in.
[[nodiscard]] std::array<float, 3> transformPoint(const std::array<float, 16>& m, const std::array<float, 3>& p) {
  return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
          m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

// Plan 0042 Milestone 3 (Plan 0042 P3): the projection of drawItems that
// computeDrawOrder() sorts. The sort point is only computed for blended
// items -- nothing else reads it.
[[nodiscard]] std::vector<std::uint32_t> drawOrderFor(std::span<const DrawItem> drawItems,
                                                      const std::optional<std::array<float, 3>>& cameraWorldPosition) {
  std::vector<DrawSortInput> sortInputs(drawItems.size());
  for (std::size_t i = 0; i < drawItems.size(); ++i) {
    if (!isBlended(drawItems[i])) continue;
    sortInputs[i].blended = true;
    sortInputs[i].worldSortPoint = transformPoint(drawItems[i].objectToWorld, drawItems[i].mesh->localBoundsCentre());
  }
  return computeDrawOrder(sortInputs, cameraWorldPosition);
}

}  // namespace

void Renderer::drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
                          atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
                          std::span<const DrawItem> drawItems, atlantis::rhi::ResourceState finalColorState,
                          atlantis::rhi::HdrColorTarget& hdrColorTarget,
                          atlantis::rhi::Buffer& fullscreenTriangleVertexBuffer,
                          atlantis::rhi::Buffer& fullscreenTriangleIndexBuffer,
                          atlantis::rhi::Pipeline& outputTransformPipeline,
                          atlantis::rhi::Sampler& outputTransformSampler,
                          float outputTransformExposureCompensationEv,
                          const EnvironmentLighting* environmentLighting, atlantis::rhi::Pipeline* skyPipeline,
                          atlantis::rhi::ShadowMap& shadowMap, atlantis::rhi::Sampler& shadowMapSampler,
                          atlantis::rhi::Pipeline& shadowCastPipeline, atlantis::rhi::Buffer& shadowLightSpaceBuffer,
                          std::span<const DrawItem> shadowCasterDrawItems,
                          const std::optional<std::array<float, 3>>& cameraWorldPosition,
                          const BloomInput* bloom) {
  // Plan 0031 (Spec 0031 Requirement 7): the one real gate for direct/
  // non-asset callers, which bypass cook/decode's own independent
  // check entirely.
  ATLANTIS_CHECK_MSG(std::isfinite(outputTransformExposureCompensationEv) &&
                          outputTransformExposureCompensationEv >= kExposureCompensationEvMin &&
                          outputTransformExposureCompensationEv <= kExposureCompensationEvMax,
                      "outputTransformExposureCompensationEv must be finite and within "
                      "[kExposureCompensationEvMin, kExposureCompensationEvMax]");
  const float exposureMultiplier = computeExposureMultiplier(outputTransformExposureCompensationEv);
  // Plan 0044 Milestone 2 (ADR-0092 / Plan P5): full BloomInput
  // validation. strength == 0 means off -- no bloom pass is declared.
  if (bloom != nullptr) {
    ATLANTIS_CHECK_MSG(std::isfinite(bloom->strength) && bloom->strength >= 0.0f && bloom->strength <= 1.0f,
                        "BloomInput::strength must be finite and in [0, 1]");
    ATLANTIS_CHECK_MSG(std::isfinite(bloom->threshold) && bloom->threshold >= 0.0f,
                        "BloomInput::threshold must be finite and >= 0");
    ATLANTIS_CHECK_MSG(std::all_of(bloom->pipelines.begin(), bloom->pipelines.end(),
                                    [](const atlantis::rhi::Pipeline* pipeline) { return pipeline != nullptr; }),
                        "BloomInput::pipelines must all be non-null");
    ATLANTIS_CHECK_MSG(bloom->targets.extent().width == hdrColorTarget.extent().width &&
                            bloom->targets.extent().height == hdrColorTarget.extent().height,
                        "BloomInput::targets' extent must equal the HdrColorTarget's extent");
  }
  const bool bloomOn = bloom != nullptr && bloom->strength > 0.0f;
  std::vector<std::uint32_t> drawOrder = drawOrderFor(drawItems, cameraWorldPosition);

  atlantis::render_graph::RenderGraphBuilder builder;
  // Plan 0024 Milestone 5 (ADR-0068 D-1/D-3): the existing single "draw"
  // pass now writes hdrResource (the scene-referred linear HDR
  // intermediate) instead of the caller's final colorTarget directly --
  // its own DrawItem loop below is otherwise byte-for-byte unchanged. A
  // new "output_transform" pass reads hdrResource (ShaderRead) and
  // writes finalColorResource -- the caller's real, final colorTarget.
  // Plan 0027 Milestone 9 (ADR-0072 D-1/P6): a new "shadow" pass writes
  // shadowMapResource; "draw" also reads it (ShaderRead) -- the same
  // write-then-read pattern hdrResource already proves. compile()'s own
  // dependency-driven topological sort (not declaration order) is what
  // structurally guarantees "shadow" executes before "draw" here, the
  // same mechanism that already orders "draw" before "output_transform"
  // below. Every pass is still declared, compiled, and executed by this
  // same, single builder/compile()/execute() call -- no second
  // CommandList, no ad hoc submit; Renderer still never calls
  // Device::submit()/Presentation::present() itself.
  const auto hdrResource = builder.declareResource("hdr_color");
  const auto depthResource = builder.declareResource("depth");
  const auto finalColorResource = builder.declareResource("final_color");
  const auto shadowMapResource = builder.declareResource("shadow_map");

  const auto shadowPass = builder.declarePass("shadow");
  builder.writes(shadowPass, shadowMapResource, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  builder.setExecute(shadowPass, [&shadowCastPipeline, &shadowLightSpaceBuffer,
                                  shadowCasterDrawItems](atlantis::rhi::CommandList& cmd) {
    cmd.bindPipeline(shadowCastPipeline);
    cmd.bindUniformBuffer(shadowLightSpaceBuffer);
    for (const DrawItem& item : shadowCasterDrawItems) {
      // Plan 0042 Milestone 3 (Spec 0042 R8): a blended surface casts no shadow.
      if (isBlended(item)) continue;
      cmd.bindVertexBuffer(item.mesh->vertexBuffer());
      cmd.bindIndexBuffer(item.mesh->indexBuffer());
      cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size() * sizeof(float));
      cmd.drawIndexed(item.mesh->indexCount());
    }
  });

  const auto drawPass = builder.declarePass("draw");
  builder.writes(drawPass, hdrResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  builder.writes(drawPass, depthResource, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  builder.reads(drawPass, shadowMapResource, atlantis::rhi::ResourceState::ShaderRead);
  builder.setExecute(drawPass, [&cameraUniformBuffer, drawItems, drawOrder = std::move(drawOrder),
                                environmentLighting, skyPipeline, &fullscreenTriangleVertexBuffer,
                                &fullscreenTriangleIndexBuffer, &shadowMap,
                                &shadowMapSampler](atlantis::rhi::CommandList& cmd) {
    // Plan 0026 Milestone 2 (ADR-0071 P5, Proposed Correction): the sky
    // draws strictly before every DrawItem below -- a correctness
    // requirement, not merely a convenience (see this function's own
    // header comment). Reuses the same fixed fullscreen-triangle
    // geometry the output-transform pass below already uses, and
    // environmentLighting's own already-realized cubemap/sampler --
    // no new caller-owned resource.
    if (skyPipeline != nullptr) {
      if (environmentLighting == nullptr) {
        ATLANTIS_CHECK_MSG(false, "a non-null skyPipeline requires frame-scoped EnvironmentLighting");
      } else {
        cmd.bindPipeline(*skyPipeline);
        cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
        cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
        cmd.bindUniformBuffer(cameraUniformBuffer);
        cmd.bindTexture(1, environmentLighting->prefilteredEnvironment, environmentLighting->environmentSampler);
        cmd.drawIndexed(3);
      }
    }

    // Plan 0042 Milestone 3 (ADR-0090 Decision 2): non-blended items in
    // caller order, then blended items back-to-front.
    for (const std::uint32_t index : drawOrder) {
      const DrawItem& item = drawItems[index];
      cmd.bindPipeline(item.material->pipeline());
      cmd.bindVertexBuffer(item.mesh->vertexBuffer());
      cmd.bindIndexBuffer(item.mesh->indexBuffer());
      cmd.bindUniformBuffer(cameraUniformBuffer);
      // Spec 0016/D3: an untextured Material (sampledTexture() == nullptr)
      // skips this call entirely -- bindTexture() is never invoked, and
      // the existing one-binding descriptor set layout path is exercised
      // exactly as before this Spec.
      if (item.material->sampledTexture() != nullptr) {
        cmd.bindTexture(1, *item.material->sampledTexture(), *item.material->sampler());
      }
      switch (item.material->environmentBinding()) {
        case MaterialEnvironmentBinding::None:
          break;
        case MaterialEnvironmentBinding::Ibl:
          if (environmentLighting == nullptr) {
            ATLANTIS_CHECK_MSG(false, "an IBL Material requires frame-scoped EnvironmentLighting");
          } else {
            cmd.bindTexture(2, environmentLighting->prefilteredEnvironment,
                            environmentLighting->environmentSampler);
            cmd.bindTexture(3, environmentLighting->dfgLut, environmentLighting->dfgSampler);
          }
          break;
      }
      // Plan 0027 Milestone 9 (ADR-0072 D-1/P6): the shadow-map binding
      // index is decided by this Material's own environmentBinding()
      // above, never a new MaterialKind -- pbr_direct_lit/pbr_ibl share
      // one MaterialPushConstantLayout::PbrDirectLit value and are
      // distinguished only by environmentBinding() (None vs Ibl), which
      // already drove the texture(2)/texture(3) binds immediately above.
      if (item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrDirectLit) {
        const std::uint32_t shadowBinding =
            item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 4U : 2U;
        cmd.bindTexture(shadowBinding, shadowMap, shadowMapSampler);
      }
      // Plan 0029 Section P14 (ADR-0074 Section 2): the normal-map
      // binding, at the same conditional-index pattern the shadow map
      // above already establishes -- reuses material.sampler(), the
      // same VkSampler handle already bound at binding 1 for base
      // color (ordinary, valid Vulkan usage, not a new RHI capability).
      // Plan 0035 Milestone 2/ADR-0081, widened by Milestones 3/4:
      // PbrClearcoat, PbrSheen, and PbrAnisotropic all have this same,
      // different binding layout -- no shadow-map binding at all (this
      // Milestone's own disclosed IBL-only, no-shadow scope, Spec 0035
      // Non-Goals), so their normal map always sits at binding 4
      // (base@1, env@2, dfg@3, normal@4) regardless of
      // environmentBinding() -- a real PbrClearcoat/PbrSheen/
      // PbrAnisotropic material is always IBL-bound this round
      // (selectShaderPair()'s own ATLANTIS_CHECK, material_realization.cpp).
      if (item.material->normalMapTexture() != nullptr) {
        std::uint32_t normalMapBinding = 0;
        if (item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrClearcoat ||
            item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrSheen ||
            item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrAnisotropic) {
          normalMapBinding = 4U;
        } else {
          normalMapBinding = item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 5U : 3U;
        }
        cmd.bindTexture(normalMapBinding, *item.material->normalMapTexture(), *item.material->sampler());
      }
      // Plan 0046 Milestone 1 (ADR-0096): the emissive texture, one binding
      // past the normal map (or where the normal map would be) -- the
      // same conditional-index pattern, through the same sampler.
      if (item.material->emissiveTexture() != nullptr) {
        std::uint32_t emissiveBinding = 0;
        if (item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrClearcoat ||
            item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrSheen ||
            item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrAnisotropic) {
          emissiveBinding = 4U;
        } else {
          emissiveBinding = item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 5U : 3U;
        }
        if (item.material->normalMapTexture() != nullptr) ++emissiveBinding;
        cmd.bindTexture(emissiveBinding, *item.material->emissiveTexture(), *item.material->sampler());
      }
      // Plan 0023 Milestone 5 (Spec 0023 D9's own Accepted Correction):
      // an exhaustive switch, no default: label -- this repository's own
      // /w14062 /WX already makes a missed MaterialPushConstantLayout
      // case a compile error, exercised here exactly as
      // selectShaderPair()'s own switch already exercises it. Never a
      // new DrawItem field (D10, reaffirmed) -- objectToWorld is read
      // from the existing item.objectToWorld in both arms.
      switch (item.material->pushConstantLayout()) {
        case MaterialPushConstantLayout::ObjectToWorldOnly:
          cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size() * sizeof(float));
          break;
        case MaterialPushConstantLayout::PbrDirectLit: {
          PbrPushConstants payload;
          std::copy(item.objectToWorld.begin(), item.objectToWorld.end(), std::begin(payload.objectToWorld));
          const auto& baseColorFactor = item.material->baseColorFactor();
          std::copy(baseColorFactor.begin(), baseColorFactor.end(), std::begin(payload.baseColorFactor));
          payload.metallicFactor = item.material->metallicFactor();
          payload.roughnessFactor = item.material->roughnessFactor();
          const auto& emissiveFactor = item.material->emissiveFactor();  // Plan 0041 Milestone 2
          std::copy(emissiveFactor.begin(), emissiveFactor.end(), std::begin(payload.emissiveFactor));
          payload.alphaCutoff = item.material->alphaCutoff();  // Plan 0042 Milestone 2
          cmd.pushConstant(&payload, sizeof(payload));
          break;
        }
        // Plan 0035 Milestone 2/ADR-0081: PbrClearcoat's own,
        // independent 96-byte payload -- objectToWorld from
        // item.objectToWorld exactly like every other arm here, never
        // a new DrawItem field (D10, reaffirmed).
        case MaterialPushConstantLayout::PbrClearcoat: {
          PbrClearcoatPushConstants payload;
          std::copy(item.objectToWorld.begin(), item.objectToWorld.end(), std::begin(payload.objectToWorld));
          const auto& baseColorFactor = item.material->baseColorFactor();
          std::copy(baseColorFactor.begin(), baseColorFactor.end(), std::begin(payload.baseColorFactor));
          payload.metallicFactor = item.material->metallicFactor();
          payload.roughnessFactor = item.material->roughnessFactor();
          payload.clearcoatFactor = item.material->clearcoatFactor();
          payload.clearcoatRoughness = item.material->clearcoatRoughness();
          const auto& emissiveFactor = item.material->emissiveFactor();
          std::copy(emissiveFactor.begin(), emissiveFactor.end(), std::begin(payload.emissiveFactor));
          payload.alphaCutoff = item.material->alphaCutoff();
          cmd.pushConstant(&payload, sizeof(payload));
          break;
        }
        // Plan 0035 Milestone 3/ADR-0081: PbrSheen's own, independent
        // 112-byte payload (PbrSheenPushConstants's own real, measured
        // layout -- see that struct's own top comment) -- objectToWorld
        // from item.objectToWorld exactly like every other arm here.
        case MaterialPushConstantLayout::PbrSheen: {
          PbrSheenPushConstants payload;
          std::copy(item.objectToWorld.begin(), item.objectToWorld.end(), std::begin(payload.objectToWorld));
          const auto& baseColorFactor = item.material->baseColorFactor();
          std::copy(baseColorFactor.begin(), baseColorFactor.end(), std::begin(payload.baseColorFactor));
          payload.metallicFactor = item.material->metallicFactor();
          payload.roughnessFactor = item.material->roughnessFactor();
          const auto& sheenColor = item.material->sheenColor();
          std::copy(sheenColor.begin(), sheenColor.end(), std::begin(payload.sheenColor));
          payload.sheenRoughness = item.material->sheenRoughness();
          const auto& emissiveFactor = item.material->emissiveFactor();
          std::copy(emissiveFactor.begin(), emissiveFactor.end(), std::begin(payload.emissiveFactor));
          payload.alphaCutoff = item.material->alphaCutoff();
          cmd.pushConstant(&payload, sizeof(payload));
          break;
        }
        // Plan 0035 Milestone 4/ADR-0081: PbrAnisotropic's own,
        // independent 96-byte payload (PbrAnisotropicPushConstants's own
        // real, measured layout -- see that struct's own top comment) --
        // objectToWorld from item.objectToWorld exactly like every other
        // arm here.
        case MaterialPushConstantLayout::PbrAnisotropic: {
          PbrAnisotropicPushConstants payload;
          std::copy(item.objectToWorld.begin(), item.objectToWorld.end(), std::begin(payload.objectToWorld));
          const auto& baseColorFactor = item.material->baseColorFactor();
          std::copy(baseColorFactor.begin(), baseColorFactor.end(), std::begin(payload.baseColorFactor));
          payload.metallicFactor = item.material->metallicFactor();
          payload.roughnessFactor = item.material->roughnessFactor();
          payload.anisotropyFactor = item.material->anisotropyFactor();
          payload.anisotropyRotation = item.material->anisotropyRotation();
          const auto& emissiveFactor = item.material->emissiveFactor();
          std::copy(emissiveFactor.begin(), emissiveFactor.end(), std::begin(payload.emissiveFactor));
          payload.alphaCutoff = item.material->alphaCutoff();
          cmd.pushConstant(&payload, sizeof(payload));
          break;
        }
      }
      cmd.drawIndexed(item.mesh->indexCount());
    }
  });

  // Plan 0044 Milestone 2 (ADR-0092): bloom's twelve-pass chain --
  // D1..D6 (downsample, D1 carries bright-pass + firefly weight), then
  // U5..U1 (upsample, tent + add), then composite. With bloom off,
  // none of these exists and the output transform reads the HDR target
  // directly (the existing-golden path). P7: the output transform
  // pipeline is the same in both cases; only what it reads changes.
  std::vector<atlantis::render_graph::ResourceHandle> bloomDownRes;
  std::vector<atlantis::render_graph::ResourceHandle> bloomUpRes;
  atlantis::render_graph::ResourceHandle bloomCompositeRes{};
  if (bloomOn) {
    for (std::size_t i = 0; i < kBloomLevelCount; ++i)
      bloomDownRes.push_back(builder.declareResource("bloom_d" + std::to_string(i + 1)));
    for (std::size_t i = 0; i < kBloomLevelCount - 1; ++i)
      bloomUpRes.push_back(builder.declareResource("bloom_u" + std::to_string(i + 1)));
    bloomCompositeRes = builder.declareResource("bloom_composite");

    // Downsample: D1 reads hdr; Dk reads D(k-1).
    for (std::size_t level = 0; level < kBloomLevelCount; ++level) {
      const auto pass = builder.declarePass("bloom_down_" + std::to_string(level + 1));
      if (level == 0)
        builder.reads(pass, hdrResource, atlantis::rhi::ResourceState::ShaderRead);
      else
        builder.reads(pass, bloomDownRes[level - 1], atlantis::rhi::ResourceState::ShaderRead);
      builder.writes(pass, bloomDownRes[level], atlantis::rhi::ResourceState::ColorAttachmentOutput);
      const bool isFirst = level == 0;
      const float thresholdValue = bloom->threshold;
      builder.setExecute(pass, [bloom, level, isFirst, thresholdValue, &hdrColorTarget,
                                &fullscreenTriangleVertexBuffer,
                                &fullscreenTriangleIndexBuffer](atlantis::rhi::CommandList& cmd) {
        cmd.bindPipeline(*bloom->pipelines[kBloomFirstDownsamplePipeline + level]);
        cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
        cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
        // Source texel size: D1 reads the HDR extent; Dk reads D(k-1)'s.
        const float srcW = static_cast<float>(
            level == 0 ? bloom->targets.extent().width : bloomLevelExtents(bloom->targets.extent())[level - 1].width);
        const float srcH = static_cast<float>(
            level == 0 ? bloom->targets.extent().height : bloomLevelExtents(bloom->targets.extent())[level - 1].height);
        const BloomDownsamplePushConstants payload{{1.0f / srcW, 1.0f / srcH}, thresholdValue,
                                                    isFirst ? 1.0f : 0.0f};
        cmd.pushConstant(&payload, sizeof(payload));
        // Binding 0: the source texture. D1 reads the HDR target; Dk reads D(k-1).
        if (level == 0)
          cmd.bindTexture(0, hdrColorTarget, bloom->targets.sampler());
        else
          cmd.bindTexture(0, bloom->targets.downsampleTarget(level - 1), bloom->targets.sampler());
        cmd.drawIndexed(3);
      });
    }

    // Upsample: U(k) reads D(k) + U(k+1); U5 reads D6 + D6 (bottom).
    for (std::size_t level = kBloomLevelCount - 1; level >= 1; --level) {
      const std::size_t uIdx = level - 1;
      const auto pass = builder.declarePass("bloom_up_" + std::to_string(level));
      builder.reads(pass, bloomDownRes[level - 1], atlantis::rhi::ResourceState::ShaderRead);
      if (level < kBloomLevelCount - 1)
        builder.reads(pass, bloomUpRes[uIdx + 1], atlantis::rhi::ResourceState::ShaderRead);
      else
        builder.reads(pass, bloomDownRes[level], atlantis::rhi::ResourceState::ShaderRead);
      builder.writes(pass, bloomUpRes[uIdx], atlantis::rhi::ResourceState::ColorAttachmentOutput);
      const float lowerW = static_cast<float>(bloomLevelExtents(bloom->targets.extent())[level].width);
      const float lowerH = static_cast<float>(bloomLevelExtents(bloom->targets.extent())[level].height);
      builder.setExecute(pass, [bloom, level, uIdx, lowerW, lowerH, &fullscreenTriangleVertexBuffer,
                                &fullscreenTriangleIndexBuffer](atlantis::rhi::CommandList& cmd) {
        cmd.bindPipeline(*bloom->pipelines[kBloomFirstUpsamplePipeline + uIdx]);
        cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
        cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
        const BloomUpsamplePushConstants payload{{1.0f / lowerW, 1.0f / lowerH}, {0.0f, 0.0f}};
        cmd.pushConstant(&payload, sizeof(payload));
        cmd.bindTexture(0, bloom->targets.downsampleTarget(level - 1), bloom->targets.sampler());
        if (level < kBloomLevelCount - 1)
          cmd.bindTexture(1, bloom->targets.upsampleTarget(uIdx + 1), bloom->targets.sampler());
        else
          cmd.bindTexture(1, bloom->targets.downsampleTarget(level), bloom->targets.sampler());
        cmd.drawIndexed(3);
      });
    }

    // Composite: hdr + strength * tent(U1) / 6.
    {
      const auto pass = builder.declarePass("bloom_composite");
      builder.reads(pass, hdrResource, atlantis::rhi::ResourceState::ShaderRead);
      builder.reads(pass, bloomUpRes[0], atlantis::rhi::ResourceState::ShaderRead);
      builder.writes(pass, bloomCompositeRes, atlantis::rhi::ResourceState::ColorAttachmentOutput);
      const float u1W = static_cast<float>(bloomLevelExtents(bloom->targets.extent())[0].width);
      const float u1H = static_cast<float>(bloomLevelExtents(bloom->targets.extent())[0].height);
      const float strengthValue = bloom->strength;
      builder.setExecute(pass, [bloom, u1W, u1H, strengthValue, &hdrColorTarget,
                                &fullscreenTriangleVertexBuffer,
                                &fullscreenTriangleIndexBuffer](atlantis::rhi::CommandList& cmd) {
        cmd.bindPipeline(*bloom->pipelines[kBloomCompositePipeline]);
        cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
        cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
        const BloomCompositePushConstants payload{{1.0f / u1W, 1.0f / u1H}, strengthValue,
                                                    1.0f / static_cast<float>(kBloomLevelCount)};
        cmd.pushConstant(&payload, sizeof(payload));
        cmd.bindTexture(0, hdrColorTarget, bloom->targets.sampler());
        cmd.bindTexture(1, bloom->targets.upsampleTarget(0), bloom->targets.sampler());
        cmd.drawIndexed(3);
      });
    }
  }

  const auto outputTransformPass = builder.declarePass("output_transform");
  if (bloomOn)
    builder.reads(outputTransformPass, bloomCompositeRes, atlantis::rhi::ResourceState::ShaderRead);
  else
    builder.reads(outputTransformPass, hdrResource, atlantis::rhi::ResourceState::ShaderRead);
  builder.writes(outputTransformPass, finalColorResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  builder.setExecute(outputTransformPass, [&hdrColorTarget, &fullscreenTriangleVertexBuffer,
                                            &fullscreenTriangleIndexBuffer, &outputTransformPipeline,
                                            &outputTransformSampler, exposureMultiplier, bloom, bloomOn](
                                               atlantis::rhi::CommandList& cmd) {
    cmd.bindPipeline(outputTransformPipeline);
    const ExposurePushConstants payload{exposureMultiplier};
    cmd.pushConstant(&payload, sizeof(payload));
    cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
    cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
    if (bloomOn)
      cmd.bindTexture(0, bloom->targets.compositeTarget(), outputTransformSampler);
    else
      cmd.bindTexture(0, hdrColorTarget, outputTransformSampler);
    cmd.drawIndexed(3);
  });

  auto compileResult = builder.compile();
  ATLANTIS_CHECK_MSG(compileResult.isOk(), "Renderer's fixed pass graph never fails to compile");

  // Plan 0027 Milestone 9 (ADR-0072 D-4): 1.0f (max depth) is what makes
  // an empty shadowCasterDrawItems list and the first frame identical --
  // both leave the shadow map at its cleared, maximum-depth value, which
  // computeShadowFactor() (pbr_direct_lit.slang/pbr_ibl.slang) always
  // reads as "not occluded."
  std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compileResult.value().resourceAt(0),
       .colorClear = kBackgroundClearColor,
       .hdrColorTarget = &hdrColorTarget,
       .finalState = std::nullopt},
      {.resource = compileResult.value().resourceAt(1),
       .depthTexture = &depthTarget,
       .depthClear = 1.0f,
       .finalState = std::nullopt},
      {.resource = compileResult.value().resourceAt(2),
       .target = &colorTarget,
       .colorClear = kBackgroundClearColor,
       .finalState = finalColorState},
      {.resource = compileResult.value().resourceAt(3),
       .depthClear = 1.0f,
       .shadowMap = &shadowMap,
       .finalState = std::nullopt},
  };
  // Plan 0044 M2: bloom resource bindings (each is an HdrColorTarget; no
  // clear -- bloom passes write over every pixel of their small targets).
  // Plan 0044 M2: bloom resource bindings by declaration index -- the four
  // fixed resources are always 0..3; bloom's D1..D6, U1..U5 and composite
  // follow at 4..15 when bloomOn. Each is an HdrColorTarget; no clear
  // value -- every bloom pass writes every pixel of its small target.
  if (bloomOn) {
    for (std::size_t i = 0; i < kBloomLevelCount; ++i) {
      bindings.push_back({.resource = compileResult.value().resourceAt(4 + i),
                          .hdrColorTarget = &bloom->targets.downsampleTarget(i),
                          .finalState = std::nullopt});
    }
    for (std::size_t i = 0; i < kBloomLevelCount - 1; ++i) {
      bindings.push_back({.resource = compileResult.value().resourceAt(4 + kBloomLevelCount + i),
                          .hdrColorTarget = &bloom->targets.upsampleTarget(i),
                          .finalState = std::nullopt});
    }
    bindings.push_back({.resource = compileResult.value().resourceAt(4 + 2 * kBloomLevelCount - 1),
                        .hdrColorTarget = &bloom->targets.compositeTarget(),
                        .finalState = std::nullopt});
  }
  atlantis::render_graph::execute(compileResult.value(), bindings, commandList);
}

}  // namespace atlantis::renderer

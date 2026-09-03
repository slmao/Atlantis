#include <atlantis/renderer/renderer.h>

#include <algorithm>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>

#include "pbr_push_constants.h"

namespace atlantis::renderer {

namespace {

// Fixed, Renderer-internal constant (not a caller-configurable parameter
// this round -- consistent with Spec 0007's own minimal-material scope; a
// future spec may expose it).
constexpr atlantis::rhi::ClearColorValue kBackgroundClearColor{0.05f, 0.05f, 0.08f, 1.0f};

}  // namespace

void Renderer::drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
                          atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
                          std::span<const DrawItem> drawItems, atlantis::rhi::ResourceState finalColorState,
                          atlantis::rhi::HdrColorTarget& hdrColorTarget,
                          atlantis::rhi::Buffer& fullscreenTriangleVertexBuffer,
                          atlantis::rhi::Buffer& fullscreenTriangleIndexBuffer,
                          atlantis::rhi::Pipeline& outputTransformPipeline,
                          atlantis::rhi::Sampler& outputTransformSampler,
                          const EnvironmentLighting* environmentLighting,
                          atlantis::rhi::Pipeline* skyPipeline) {
  atlantis::render_graph::RenderGraphBuilder builder;
  // Plan 0024 Milestone 5 (ADR-0068 D-1/D-3): the existing single "draw"
  // pass now writes hdrResource (the scene-referred linear HDR
  // intermediate) instead of the caller's final colorTarget directly --
  // its own DrawItem loop below is otherwise byte-for-byte unchanged. A
  // new "output_transform" pass reads hdrResource (ShaderRead) and
  // writes finalColorResource -- the caller's real, final colorTarget.
  // Both passes are declared, compiled, and executed by this same,
  // single builder/compile()/execute() call -- no second CommandList,
  // no ad hoc submit; Renderer still never calls Device::submit()/
  // Presentation::present() itself.
  const auto hdrResource = builder.declareResource("hdr_color");
  const auto depthResource = builder.declareResource("depth");
  const auto finalColorResource = builder.declareResource("final_color");

  const auto drawPass = builder.declarePass("draw");
  builder.writes(drawPass, hdrResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  builder.writes(drawPass, depthResource, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  builder.setExecute(drawPass, [&commandList, &cameraUniformBuffer, drawItems, environmentLighting, skyPipeline,
                                &fullscreenTriangleVertexBuffer,
                                &fullscreenTriangleIndexBuffer](atlantis::rhi::CommandList& cmd) {
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

    for (const DrawItem& item : drawItems) {
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
          cmd.pushConstant(&payload, sizeof(payload));
          break;
        }
      }
      cmd.drawIndexed(item.mesh->indexCount());
    }
  });

  const auto outputTransformPass = builder.declarePass("output_transform");
  builder.reads(outputTransformPass, hdrResource, atlantis::rhi::ResourceState::ShaderRead);
  builder.writes(outputTransformPass, finalColorResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  builder.setExecute(outputTransformPass, [&commandList, &hdrColorTarget, &fullscreenTriangleVertexBuffer,
                                            &fullscreenTriangleIndexBuffer, &outputTransformPipeline,
                                            &outputTransformSampler](atlantis::rhi::CommandList& cmd) {
    cmd.bindPipeline(outputTransformPipeline);
    cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
    cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
    cmd.bindTexture(0, hdrColorTarget, outputTransformSampler);
    cmd.drawIndexed(3);
  });

  auto compileResult = builder.compile();
  ATLANTIS_CHECK_MSG(compileResult.isOk(), "Renderer's fixed two-pass graph never fails to compile");

  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compileResult.value().resourceAt(0),
       .colorClear = kBackgroundClearColor,
       .hdrColorTarget = &hdrColorTarget},
      {.resource = compileResult.value().resourceAt(1), .depthTexture = &depthTarget, .depthClear = 1.0f},
      {.resource = compileResult.value().resourceAt(2),
       .target = &colorTarget,
       .colorClear = kBackgroundClearColor,
       .finalState = finalColorState},
  };
  atlantis::render_graph::execute(compileResult.value(), bindings, commandList);
}

}  // namespace atlantis::renderer

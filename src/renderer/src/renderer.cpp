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
                          atlantis::rhi::Sampler& outputTransformSampler) {
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
  builder.setExecute(drawPass, [&commandList, &cameraUniformBuffer, drawItems](atlantis::rhi::CommandList& cmd) {
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

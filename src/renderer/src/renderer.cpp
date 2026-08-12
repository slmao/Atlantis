#include <atlantis/renderer/renderer.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>

namespace atlantis::renderer {

namespace {

// Fixed, Renderer-internal constant (not a caller-configurable parameter
// this round -- consistent with Spec 0007's own minimal-material scope; a
// future spec may expose it).
constexpr atlantis::rhi::ClearColorValue kBackgroundClearColor{0.05f, 0.05f, 0.08f, 1.0f};

}  // namespace

void Renderer::drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
                          atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
                          std::span<const DrawItem> drawItems) {
  atlantis::render_graph::RenderGraphBuilder builder;
  const auto colorResource = builder.declareResource("color");
  const auto depthResource = builder.declareResource("depth");
  const auto pass = builder.declarePass("draw");
  builder.writes(pass, colorResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  builder.writes(pass, depthResource, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  builder.setExecute(pass, [&commandList, &cameraUniformBuffer, drawItems](atlantis::rhi::CommandList& cmd) {
    for (const DrawItem& item : drawItems) {
      cmd.bindPipeline(item.material->pipeline());
      cmd.bindVertexBuffer(item.mesh->vertexBuffer());
      cmd.bindIndexBuffer(item.mesh->indexBuffer());
      cmd.bindUniformBuffer(cameraUniformBuffer);
      cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size() * sizeof(float));
      cmd.drawIndexed(item.mesh->indexCount());
    }
  });

  auto compileResult = builder.compile();
  ATLANTIS_CHECK_MSG(compileResult.isOk(), "Renderer's fixed one-pass graph never fails to compile");

  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {compileResult.value().resourceAt(0), &colorTarget, kBackgroundClearColor, nullptr, 0.0f},
      {compileResult.value().resourceAt(1), nullptr, atlantis::rhi::ClearColorValue{}, &depthTarget, 1.0f},
  };
  atlantis::render_graph::execute(compileResult.value(), bindings, commandList);
}

}  // namespace atlantis::renderer

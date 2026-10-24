#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fake_command_list.h"

// Plan 0066 M3a (ADR-0133 D3): a binding that loads its attachment -- the
// scope begins loaded, a same-state barrier makes the earlier writes visible
// before each pass on it, a depth binding's incomingState seeds its tracking,
// a Load that has nothing to load is refused, and execute() reports where it
// left each bound resource. A Clear binding is recorded exactly as before.
// No Vulkan device anywhere in this test binary.

namespace {

using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeHdrColorTarget;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeTexture;
using atlantis::rhi::AttachmentLoad;
using atlantis::rhi::ResourceState;
using EventKind = FakeCommandList::EventKind;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as attachment_execution_tests.cpp's.
class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<RecordedFailure>& recorded)
      : previous_(atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
          recorded.push_back({std::string(info.expression), std::string(info.message)});
        })) {}

  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

[[nodiscard]] bool anyMessageContains(const std::vector<RecordedFailure>& failures, const std::string& text) {
  for (const RecordedFailure& failure : failures) {
    if (failure.message.find(text) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("a Clear binding is recorded exactly as before: cleared, no same-state barrier",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto pass = builder.declarePass("draw");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget target{"target"};
  FakeCommandList commandList;
  const auto endStates = execute(compiled.value(),
                                 {{.resource = compiled.value().resourceAt(0),
                                   .target = &target,
                                   .finalState = ResourceState::PresentSource}},
                                 commandList);

  REQUIRE(commandList.transitions.size() == 2);
  CHECK(commandList.transitions[0].before == ResourceState::Undefined);
  CHECK(commandList.transitions[0].after == ResourceState::ColorAttachmentOutput);
  CHECK(commandList.transitions[1].after == ResourceState::PresentSource);
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.beginRenderingCalls[0].colorLoad == AttachmentLoad::Clear);
  CHECK(endStates == std::vector<ResourceState>{ResourceState::PresentSource});
}

TEST_CASE("a Load target begins loaded after a same-state barrier, then reaches its finalState",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto pass = builder.declarePass("overlay");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget target{"target"};
  FakeCommandList commandList;
  const auto endStates = execute(compiled.value(),
                                 {{.resource = compiled.value().resourceAt(0),
                                   .target = &target,
                                   .incomingState = ResourceState::ColorAttachmentOutput,
                                   .finalState = ResourceState::PresentSource,
                                   .load = AttachmentLoad::Load}},
                                 commandList);

  CHECK(commandList.events == std::vector<EventKind>{EventKind::Transition, EventKind::BeginRendering,
                                                     EventKind::EndRendering, EventKind::Transition});
  REQUIRE(commandList.transitions.size() == 2);
  CHECK(commandList.transitions[0].before == ResourceState::ColorAttachmentOutput);
  CHECK(commandList.transitions[0].after == ResourceState::ColorAttachmentOutput);  // the same-state barrier
  CHECK(commandList.transitions[1].before == ResourceState::ColorAttachmentOutput);
  CHECK(commandList.transitions[1].after == ResourceState::PresentSource);
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.beginRenderingCalls[0].colorLoad == AttachmentLoad::Load);
  CHECK(commandList.beginRenderingCalls[0].depthLoad == AttachmentLoad::Clear);
  CHECK(endStates == std::vector<ResourceState>{ResourceState::PresentSource});
}

TEST_CASE("a Load depth begins loaded after its same-state barrier; a Clear target beside it clears",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto depth = builder.declareResource("depth");
  const auto pass = builder.declarePass("tested");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  builder.writes(pass, depth, ResourceState::DepthAttachmentReadWrite);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget target{"target"};
  FakeTexture depthTexture{"depth"};
  FakeCommandList commandList;
  const auto endStates = execute(compiled.value(),
                                 {{.resource = compiled.value().resourceAt(0),
                                   .target = &target,
                                   .finalState = ResourceState::TransferSource},
                                  {.resource = compiled.value().resourceAt(1),
                                   .depthTexture = &depthTexture,
                                   .incomingState = ResourceState::DepthAttachmentReadWrite,
                                   .load = AttachmentLoad::Load}},
                                 commandList);

  REQUIRE(commandList.textureTransitions.size() == 1);
  CHECK(commandList.textureTransitions[0].target == &depthTexture);
  CHECK(commandList.textureTransitions[0].before == ResourceState::DepthAttachmentReadWrite);
  CHECK(commandList.textureTransitions[0].after == ResourceState::DepthAttachmentReadWrite);
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.beginRenderingCalls[0].depth == &depthTexture);
  CHECK(commandList.beginRenderingCalls[0].colorLoad == AttachmentLoad::Clear);
  CHECK(commandList.beginRenderingCalls[0].depthLoad == AttachmentLoad::Load);
  CHECK(endStates == std::vector<ResourceState>{ResourceState::TransferSource, ResourceState::DepthAttachmentReadWrite});
}

// One writer per resource per graph (Spec 0005), so passes that continue a
// target are chained graphs: each binds the end state the previous reported.
TEST_CASE("chained graphs on a Load target: each gets its own barrier, from the previous reported end state",
          "[render_graph][execution][attachment_load]") {
  FakeRenderTarget target{"target"};
  FakeCommandList commandList;
  ResourceState incoming = ResourceState::ColorAttachmentOutput;
  for (const char* label : {"outline", "on_top"}) {
    RenderGraphBuilder builder;
    const auto color = builder.declareResource("color");
    const auto pass = builder.declarePass(label);
    builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
    const auto compiled = builder.compile();
    REQUIRE(compiled.isOk());
    const auto endStates = execute(compiled.value(),
                                   {{.resource = compiled.value().resourceAt(0),
                                     .target = &target,
                                     .incomingState = incoming,
                                     .load = AttachmentLoad::Load}},
                                   commandList);
    REQUIRE(endStates.size() == 1);
    incoming = endStates[0];
  }

  CHECK(commandList.events == std::vector<EventKind>{EventKind::Transition, EventKind::BeginRendering,
                                                     EventKind::EndRendering, EventKind::Transition,
                                                     EventKind::BeginRendering, EventKind::EndRendering});
  for (const auto& transition : commandList.transitions) {
    CHECK(transition.before == ResourceState::ColorAttachmentOutput);
    CHECK(transition.after == ResourceState::ColorAttachmentOutput);
  }
  REQUIRE(commandList.beginRenderingCalls.size() == 2);
  CHECK(commandList.beginRenderingCalls[0].colorLoad == AttachmentLoad::Load);
  CHECK(commandList.beginRenderingCalls[1].colorLoad == AttachmentLoad::Load);
  CHECK(incoming == ResourceState::ColorAttachmentOutput);
}

TEST_CASE("a depth binding's incomingState seeds its tracking: no transition when it already matches",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto depth = builder.declareResource("depth");
  const auto pass = builder.declarePass("draw");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  builder.writes(pass, depth, ResourceState::DepthAttachmentReadWrite);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget target{"target"};
  FakeTexture depthTexture{"depth"};
  FakeCommandList commandList;
  static_cast<void>(execute(compiled.value(),
                            {{.resource = compiled.value().resourceAt(0), .target = &target},
                             {.resource = compiled.value().resourceAt(1),
                              .depthTexture = &depthTexture,
                              .incomingState = ResourceState::DepthAttachmentReadWrite}},
                            commandList));

  CHECK(commandList.textureTransitions.empty());  // Clear: no barrier, and no Undefined -> DepthAttachmentReadWrite
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  CHECK(commandList.beginRenderingCalls[0].depthLoad == AttachmentLoad::Clear);
}

TEST_CASE("execute() reports each binding's end state: finalState, last usage, or incomingState when unused",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto depth = builder.declareResource("depth");
  const auto unused = builder.declareResource("unused");
  const auto pass = builder.declarePass("draw");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  builder.writes(pass, depth, ResourceState::DepthAttachmentReadWrite);
  static_cast<void>(unused);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget target{"target"};
  FakeRenderTarget idle{"idle"};
  FakeTexture depthTexture{"depth"};
  FakeCommandList commandList;
  const auto endStates = execute(compiled.value(),
                                 {{.resource = compiled.value().resourceAt(2),
                                   .target = &idle,
                                   .incomingState = ResourceState::ShaderRead,
                                   .finalState = ResourceState::PresentSource},
                                  {.resource = compiled.value().resourceAt(0),
                                   .target = &target,
                                   .finalState = ResourceState::ShaderRead},
                                  {.resource = compiled.value().resourceAt(1), .depthTexture = &depthTexture}},
                                 commandList);

  // In bindings' order, not the graph's: the unused one keeps its incomingState (no trailing transition).
  CHECK(endStates == std::vector<ResourceState>{ResourceState::ShaderRead, ResourceState::ShaderRead,
                                                ResourceState::DepthAttachmentReadWrite});
  for (const auto& transition : commandList.transitions) CHECK(transition.target != &idle);
}

TEST_CASE("a Load binding entering Undefined is refused -- there is nothing to load",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto pass = builder.declarePass("draw");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget target{"target"};
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);
  static_cast<void>(execute(
      compiled.value(), {{.resource = compiled.value().resourceAt(0), .target = &target, .load = AttachmentLoad::Load}},
      commandList));
  CHECK(anyMessageContains(failures, "nothing to load"));
}

TEST_CASE("only a target or depthTexture binding loads; an HdrColorTarget scope cannot load its depth",
          "[render_graph][execution][attachment_load]") {
  RenderGraphBuilder builder;
  const auto color = builder.declareResource("hdr");
  const auto depth = builder.declareResource("depth");
  const auto pass = builder.declarePass("draw");
  builder.writes(pass, color, ResourceState::ColorAttachmentOutput);
  builder.writes(pass, depth, ResourceState::DepthAttachmentReadWrite);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeHdrColorTarget hdr{"hdr"};
  FakeTexture depthTexture{"depth"};
  {
    FakeCommandList commandList;
    std::vector<RecordedFailure> failures;
    ScopedFailureHandler handler(failures);
    static_cast<void>(execute(compiled.value(),
                              {{.resource = compiled.value().resourceAt(0),
                                .hdrColorTarget = &hdr,
                                .incomingState = ResourceState::ColorAttachmentOutput,
                                .load = AttachmentLoad::Load},
                               {.resource = compiled.value().resourceAt(1), .depthTexture = &depthTexture}},
                              commandList));
    CHECK(anyMessageContains(failures, "only a target or depthTexture binding can load"));
  }
  {
    FakeCommandList commandList;
    std::vector<RecordedFailure> failures;
    ScopedFailureHandler handler(failures);
    static_cast<void>(execute(compiled.value(),
                              {{.resource = compiled.value().resourceAt(0), .hdrColorTarget = &hdr},
                               {.resource = compiled.value().resourceAt(1),
                                .depthTexture = &depthTexture,
                                .incomingState = ResourceState::DepthAttachmentReadWrite,
                                .load = AttachmentLoad::Load}},
                              commandList));
    CHECK(anyMessageContains(failures, "cannot load its depth attachment"));
  }
}

// ADR-0133 D1: the overload is not pure. A CommandList that implements only the
// calls before ADR-0133 stays valid: both-Clear forwards to its four-parameter
// beginRendering(), and a Load is refused.
TEST_CASE("the default load overload forwards both-Clear and refuses a Load", "[render_graph][rhi][attachment_load]") {
  struct ClearOnly final : atlantis::rhi::CommandList {
    using atlantis::rhi::CommandList::beginRendering;
    int fourParameterCalls = 0;
    void transitionResource(atlantis::rhi::RenderTarget&, ResourceState, ResourceState) override {}
    void clearColor(atlantis::rhi::RenderTarget&, atlantis::rhi::ClearColorValue) override {}
    void transitionResource(atlantis::rhi::Texture&, ResourceState, ResourceState) override {}
    void beginRendering(atlantis::rhi::RenderTarget&, atlantis::rhi::Texture*, atlantis::rhi::ClearColorValue,
                        float) override {
      ++fourParameterCalls;
    }
    void endRendering() override {}
    void bindPipeline(atlantis::rhi::Pipeline&) override {}
    void bindVertexBuffer(atlantis::rhi::Buffer&) override {}
    void bindIndexBuffer(atlantis::rhi::Buffer&) override {}
    void bindUniformBuffer(atlantis::rhi::Buffer&) override {}
    void pushConstant(const void*, std::size_t) override {}
    void drawIndexed(std::uint32_t) override {}
    void copyRenderTargetToBuffer(atlantis::rhi::RenderTarget&, atlantis::rhi::Buffer&) override {}
    void transitionResource(atlantis::rhi::SampledTexture&, ResourceState, ResourceState) override {}
    void copyBufferToTexture(atlantis::rhi::Buffer&, atlantis::rhi::SampledTexture&) override {}
    void copyBufferToTexture(atlantis::rhi::Buffer&, atlantis::rhi::SampledTexture&,
                             std::span<const atlantis::rhi::SampledTextureUploadRegion>) override {}
    void bindTexture(std::uint32_t, const atlantis::rhi::SampledTexture&, const atlantis::rhi::Sampler&) override {}
    void transitionResource(atlantis::rhi::HdrColorTarget&, ResourceState, ResourceState) override {}
    void beginRendering(atlantis::rhi::HdrColorTarget&, atlantis::rhi::Texture*, atlantis::rhi::ClearColorValue,
                        float) override {}
    void bindTexture(std::uint32_t, const atlantis::rhi::HdrColorTarget&, const atlantis::rhi::Sampler&) override {}
    void transitionResource(atlantis::rhi::ShadowMap&, ResourceState, ResourceState) override {}
    void beginRendering(atlantis::rhi::ShadowMap&, float) override {}
    void bindTexture(std::uint32_t, const atlantis::rhi::ShadowMap&, const atlantis::rhi::Sampler&) override {}
    void bindTexture(std::uint32_t, const atlantis::rhi::RenderTarget&, const atlantis::rhi::Sampler&) override {}
    void setScissor(atlantis::rhi::Rect2D) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
  };
  ClearOnly list;
  FakeRenderTarget target{"target"};
  FakeTexture depth{"depth"};
  list.beginRendering(target, &depth, {}, 1.0f, AttachmentLoad::Clear, AttachmentLoad::Clear);
  CHECK(list.fourParameterCalls == 1);

  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);
  list.beginRendering(target, &depth, {}, 1.0f, AttachmentLoad::Clear, AttachmentLoad::Load);
  CHECK(anyMessageContains(failures, "cannot load an attachment"));
}

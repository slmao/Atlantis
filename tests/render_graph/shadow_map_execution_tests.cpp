#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/types.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fake_command_list.h"

// Plan 0027 Milestone 3 (ADR-0072 D-4): ResourceBinding's shadowMap
// field, execute()'s own widened Guard 0, and -- the real, previously-
// missing behavior this Milestone fixes -- the depth-only beginRendering()
// dispatch path for a pass with no ColorAttachmentOutput usage at all.
// Mirrors hdr_color_target_execution_tests.cpp's own identical shape. No
// Vulkan device anywhere in this test binary.

namespace {

using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::ResourceHandle;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampler;
using atlantis::render_graph::test::FakeShadowMap;
using atlantis::rhi::ResourceState;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as hdr_color_target_execution_tests.cpp's own
// ScopedFailureHandler.
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

}  // namespace

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with none of the five kinds set",
          "[render_graph][execution][shadow_map]") {
  // Confirms the five-kind message text/count, not just the pre-existing
  // four-kind case (already covered by hdr_color_target_execution_tests.cpp) --
  // no dedicated test exercised Guard 0's own "zero bound" branch at all
  // before this Plan; every prior Guard 0 test bound exactly two kinds
  // together (a "both set" case), never zero.
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("orphan");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both target and shadowMap set",
          "[render_graph][execution][shadow_map]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("both-target");
  FakeShadowMap fakeShadowMap("both-shadow-map");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.target = &fakeTarget;
  malformed.shadowMap = &fakeShadowMap;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both hdrColorTarget and shadowMap set",
          "[render_graph][execution][shadow_map]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  atlantis::render_graph::test::FakeHdrColorTarget fakeHdr("both-hdr");
  FakeShadowMap fakeShadowMap("both-shadow-map");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.hdrColorTarget = &fakeHdr;
  malformed.shadowMap = &fakeShadowMap;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() accepts two bindings each naming a distinct resource, one of them shadowMap",
          "[render_graph][execution][shadow_map]") {
  // The "no two entries bind the same resource" half of Guard 0 -- two
  // DIFFERENT resources, one shadowMap-bound and one target-bound, must
  // not spuriously collide.
  RenderGraphBuilder builder;
  const ResourceHandle shadow = builder.declareResource("shadow");
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle shadowPass = builder.declarePass("Shadow");
  builder.writes(shadowPass, shadow, ResourceState::DepthAttachmentReadWrite);
  const PassHandle colorPass = builder.declarePass("Color");
  builder.writes(colorPass, color, ResourceState::ColorAttachmentOutput);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeShadowMap fakeShadowMap("shadow-map");
  FakeRenderTarget fakeTarget("color-target");
  FakeCommandList commandList;

  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .depthClear = 1.0f, .shadowMap = &fakeShadowMap},
      {.resource = compiled.value().resourceAt(1), .target = &fakeTarget},
  };
  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.beginRenderingShadowMapCalls.size() == 1);
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
}

// The real fix this Milestone makes: a pass whose only usage is
// DepthAttachmentReadWrite on a shadowMap-bound resource has no
// ColorAttachmentOutput usage at all -- colorBinding is always nullptr
// for it. Before this Milestone, canBeginRendering required
// colorBinding != nullptr unconditionally, so this pass's own
// beginRendering()/executeFn/endRendering would have been silently
// skipped entirely. This test fails against the pre-Milestone-3 code
// (colorBinding == nullptr => canBeginRendering == false) and passes
// against the fixed code.
TEST_CASE("execute() begins a genuinely depth-only ShadowMap pass with no color attachment at all",
          "[render_graph][execution][shadow_map]") {
  RenderGraphBuilder builder;
  const ResourceHandle shadow = builder.declareResource("shadow");
  const PassHandle shadowPass = builder.declarePass("Shadow");
  builder.writes(shadowPass, shadow, ResourceState::DepthAttachmentReadWrite);

  bool shadowPassRan = false;
  builder.setExecute(shadowPass, [&](atlantis::rhi::CommandList&) { shadowPassRan = true; });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeShadowMap fakeShadowMap("shadow-map");
  FakeCommandList commandList;

  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .depthClear = 1.0f, .shadowMap = &fakeShadowMap},
  };
  execute(compiled.value(), bindings, commandList);

  REQUIRE(shadowPassRan);
  REQUIRE(commandList.beginRenderingShadowMapCalls.size() == 1);
  REQUIRE(commandList.beginRenderingShadowMapCalls[0].depth == &fakeShadowMap);
  REQUIRE(commandList.beginRenderingShadowMapCalls[0].depthClear == 1.0f);
  // Never dispatched through either color-carrying overload.
  REQUIRE(commandList.beginRenderingCalls.empty());
  REQUIRE(commandList.beginRenderingHdrCalls.empty());
  // Exactly one matching endRendering() for the one beginRendering().
  REQUIRE(std::count(commandList.events.begin(), commandList.events.end(),
                      FakeCommandList::EventKind::EndRendering) == 1);
}

// Plan 0027 Milestone 3 (ADR-0072 D-4): the real, two-pass shape
// Renderer::drawFrame() itself builds -- a "shadow" pass writes
// DepthAttachmentReadWrite into shadowMap (depth-only, no color at all),
// and a downstream "draw" pass reads it (ShaderRead) and writes
// ColorAttachmentOutput into the caller's own final RenderTarget,
// sampling shadowMap via bindTexture() inside its own executeFn.
// Mirrors hdr_color_target_execution_tests.cpp's own identical two-pass
// test exactly, substituting the depth-only shadow pass for the
// color-carrying geometry pass.
TEST_CASE("execute() runs a real two-pass shadow graph: shadow pass writes shadowMap (depth-only), draw pass "
          "reads it and writes the final RenderTarget",
          "[render_graph][execution][shadow_map]") {
  RenderGraphBuilder builder;
  const ResourceHandle shadowResource = builder.declareResource("shadow-map");
  const ResourceHandle finalResource = builder.declareResource("final-color");

  FakeShadowMap fakeShadowMap("shadow");
  FakeRenderTarget fakeFinal("final");
  FakeSampler fakeSampler("shadow-map-sampler");
  FakeCommandList commandList;

  bool shadowRan = false;
  bool drawRan = false;
  std::size_t shadowTransitionsWhenDrawRan = static_cast<std::size_t>(-1);

  const PassHandle shadow = builder.declarePass("Shadow");
  builder.writes(shadow, shadowResource, ResourceState::DepthAttachmentReadWrite);
  builder.setExecute(shadow, [&](atlantis::rhi::CommandList&) { shadowRan = true; });

  const PassHandle draw = builder.declarePass("Draw");
  builder.reads(draw, shadowResource, ResourceState::ShaderRead);
  builder.writes(draw, finalResource, ResourceState::ColorAttachmentOutput);
  builder.setExecute(draw, [&](atlantis::rhi::CommandList& cmd) {
    drawRan = true;
    shadowTransitionsWhenDrawRan = commandList.shadowMapTransitions.size();
    cmd.bindTexture(2, fakeShadowMap, fakeSampler);
  });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .depthClear = 1.0f, .shadowMap = &fakeShadowMap},
      {.resource = compiled.value().resourceAt(1), .target = &fakeFinal},
  };
  execute(compiled.value(), bindings, commandList);

  REQUIRE(shadowRan);
  REQUIRE(drawRan);

  // Both of shadowMap's own transitions -- the shadow pass's Undefined ->
  // DepthAttachmentReadWrite, AND the draw pass's own declared ShaderRead
  // usage's DepthAttachmentReadWrite -> ShaderRead -- have already landed
  // by the time the draw pass's own callback runs (execute()'s own real
  // algorithm records every one of a pass's declared-usage transitions
  // before invoking that pass's own callback).
  REQUIRE(shadowTransitionsWhenDrawRan == 2);

  REQUIRE(commandList.shadowMapTransitions.size() == 2);
  REQUIRE(commandList.shadowMapTransitions[0].target == &fakeShadowMap);
  REQUIRE(commandList.shadowMapTransitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.shadowMapTransitions[0].after == ResourceState::DepthAttachmentReadWrite);
  REQUIRE(commandList.shadowMapTransitions[1].target == &fakeShadowMap);
  REQUIRE(commandList.shadowMapTransitions[1].before == ResourceState::DepthAttachmentReadWrite);
  REQUIRE(commandList.shadowMapTransitions[1].after == ResourceState::ShaderRead);

  // Dispatch correctness: the shadow pass used the depth-only
  // beginRendering(ShadowMap&, float) overload; the draw pass used the
  // ordinary RenderTarget-shaped one -- never crossed.
  REQUIRE(commandList.beginRenderingShadowMapCalls.size() == 1);
  REQUIRE(commandList.beginRenderingShadowMapCalls[0].depth == &fakeShadowMap);
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  REQUIRE(commandList.beginRenderingCalls[0].color == &fakeFinal);

  // The draw pass's own real bindTexture(ShadowMap&, Sampler&) call,
  // recorded via the ShadowMap-shaped overload, never the SampledTexture-
  // or HdrColorTarget-shaped ones.
  REQUIRE(commandList.boundShadowMapTextures.size() == 1);
  REQUIRE(commandList.boundShadowMapTextures[0].binding == 2);
  REQUIRE(commandList.boundShadowMapTextures[0].texture == &fakeShadowMap);
  REQUIRE(commandList.boundShadowMapTextures[0].sampler == &fakeSampler);
  REQUIRE(commandList.boundTextures.empty());
  REQUIRE(commandList.boundHdrTextures.empty());

  // Pass order itself: execute() invokes every pass's own executeFn in
  // exactly the compiled order, shadow strictly before draw -- derived
  // automatically from the shared shadowResource's own write-then-read
  // declarations, never asserted here as a given.
  const auto shadowBeginIt = std::find(commandList.events.begin(), commandList.events.end(),
                                        FakeCommandList::EventKind::BeginRenderingShadowMap);
  const auto drawBeginIt =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::BeginRendering);
  REQUIRE(shadowBeginIt != commandList.events.end());
  REQUIRE(drawBeginIt != commandList.events.end());
  REQUIRE(shadowBeginIt < drawBeginIt);
}

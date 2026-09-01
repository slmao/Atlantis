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

// Plan 0024 Milestone 8 (ADR-0068 D-1/D-3): ResourceBinding's
// hdrColorTarget field and execute()'s own widened Guard 0/dispatch --
// mirrors sampled_texture_execution_tests.cpp's own identical shape
// (Spec 0016's two-to-three-kind widening is this file's own direct
// template, ADR-0068 D-3's own explicit citation), widened one further
// kind, to four. No Vulkan device anywhere in this test binary.

namespace {

using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::ResourceHandle;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeHdrColorTarget;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampledTexture;
using atlantis::render_graph::test::FakeSampler;
using atlantis::render_graph::test::FakeTexture;
using atlantis::rhi::ResourceState;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as sampled_texture_execution_tests.cpp's own
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

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with none of target/depthTexture/sampledTexture/"
          "hdrColorTarget set",
          "[render_graph][execution][hdr_color_target]") {
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

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both target and hdrColorTarget set",
          "[render_graph][execution][hdr_color_target]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("both-target");
  FakeHdrColorTarget fakeHdr("both-hdr");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.target = &fakeTarget;
  malformed.hdrColorTarget = &fakeHdr;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both depthTexture and hdrColorTarget set",
          "[render_graph][execution][hdr_color_target]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeTexture fakeDepth("both-depth");
  FakeHdrColorTarget fakeHdr("both-hdr");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.depthTexture = &fakeDepth;
  malformed.hdrColorTarget = &fakeHdr;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both sampledTexture and hdrColorTarget set",
          "[render_graph][execution][hdr_color_target]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeSampledTexture fakeSampled("both-sampled");
  FakeHdrColorTarget fakeHdr("both-hdr");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.sampledTexture = &fakeSampled;
  malformed.hdrColorTarget = &fakeHdr;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() never recognizes an hdrColorTarget-carrying pass with no depth as a RenderTarget draw pass",
          "[render_graph][execution][hdr_color_target]") {
  // Mirrors sampled_texture_execution_tests.cpp's own identical "never
  // recognized as a draw pass" test -- here confirming the OPPOSITE
  // direction: an hdrColorTarget-carrying pass IS its own, distinct
  // draw-pass overload (beginRendering(HdrColorTarget&, ...)), never
  // silently dispatched through the RenderTarget-shaped
  // beginRendering(RenderTarget&, ...) overload.
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("hdr-write");
  const PassHandle geometry = builder.declarePass("Geometry");
  builder.writes(geometry, resource, ResourceState::ColorAttachmentOutput);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeHdrColorTarget fakeHdr("hdr");
  FakeCommandList commandList;

  ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.hdrColorTarget = &fakeHdr;
  execute(compiled.value(), {binding}, commandList);

  REQUIRE(commandList.beginRenderingCalls.empty());
  REQUIRE(commandList.beginRenderingHdrCalls.size() == 1);
  REQUIRE(commandList.beginRenderingHdrCalls[0].color == &fakeHdr);
  REQUIRE(commandList.beginRenderingHdrCalls[0].depth == nullptr);
}

// Plan 0024 Milestone 8 (ADR-0068 D-1/D-3): the real, two-pass shape
// Renderer::drawFrame() itself builds -- a "geometry" pass writes
// ColorAttachmentOutput into hdrColorTarget, and a downstream "output-
// transform" pass reads it (ShaderRead) and writes ColorAttachmentOutput
// into the caller's own final RenderTarget, sampling hdrColorTarget via
// bindTexture() inside its own executeFn (mirroring exactly what
// Renderer's real output-transform pass callback does). Confirms the
// full real transition sequence (Undefined -> ColorAttachmentOutput ->
// ShaderRead) and dispatch, all with no Vulkan device or Renderer
// anywhere in this test binary -- the dependency ordering itself
// (geometry pass before output-transform pass) is derived automatically
// from the shared resource's own write-then-read usage declarations
// (ADR-0017/ADR-0018), never asserted here as an assumption.
TEST_CASE("execute() runs a real two-pass HDR graph: geometry pass writes hdrColorTarget, output-transform pass "
          "reads it and writes the final RenderTarget",
          "[render_graph][execution][hdr_color_target]") {
  RenderGraphBuilder builder;
  const ResourceHandle hdrResource = builder.declareResource("hdr-color");
  const ResourceHandle finalResource = builder.declareResource("final-color");

  FakeHdrColorTarget fakeHdr("hdr");
  FakeRenderTarget fakeFinal("final");
  FakeSampler fakeSampler("output-transform-sampler");
  FakeCommandList commandList;

  bool geometryRan = false;
  bool outputTransformRan = false;
  std::size_t hdrTransitionsWhenOutputTransformRan = static_cast<std::size_t>(-1);

  const PassHandle geometry = builder.declarePass("Geometry");
  builder.writes(geometry, hdrResource, ResourceState::ColorAttachmentOutput);
  builder.setExecute(geometry, [&](atlantis::rhi::CommandList&) { geometryRan = true; });

  const PassHandle outputTransform = builder.declarePass("OutputTransform");
  builder.reads(outputTransform, hdrResource, ResourceState::ShaderRead);
  builder.writes(outputTransform, finalResource, ResourceState::ColorAttachmentOutput);
  builder.setExecute(outputTransform, [&](atlantis::rhi::CommandList& cmd) {
    outputTransformRan = true;
    hdrTransitionsWhenOutputTransformRan = commandList.hdrColorTargetTransitions.size();
    cmd.bindTexture(fakeHdr, fakeSampler);
  });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .hdrColorTarget = &fakeHdr},
      {.resource = compiled.value().resourceAt(1), .target = &fakeFinal},
  };
  execute(compiled.value(), bindings, commandList);

  REQUIRE(geometryRan);
  REQUIRE(outputTransformRan);

  // Both of hdrColorTarget's own transitions -- geometry's Undefined ->
  // ColorAttachmentOutput, AND the output-transform pass's own declared
  // ShaderRead usage's ColorAttachmentOutput -> ShaderRead -- have
  // already landed by the time the output-transform pass's own
  // callback runs. Unlike sampled_texture_execution_tests.cpp's own
  // "only the first transition lands before the callback" case (there,
  // the second transition is a trailing ResourceBinding::finalState,
  // applied only after every pass's own callback has run), here the
  // ShaderRead transition is a declared usage ON the output-transform
  // pass itself -- execute()'s own real algorithm records every one of
  // a pass's declared-usage transitions before invoking THAT pass's own
  // callback, so hdrColorTarget is already correctly in ShaderRead
  // layout by the time this callback's own bindTexture() call runs.
  REQUIRE(hdrTransitionsWhenOutputTransformRan == 2);

  // The full real sequence: Undefined -> ColorAttachmentOutput
  // (geometry pass writes), then ColorAttachmentOutput -> ShaderRead
  // (output-transform pass reads) -- no trailing transition (no
  // finalState set on the hdrColorTarget binding, matching Renderer's
  // own real usage: the intermediate is never read outside this one
  // drawFrame() call).
  REQUIRE(commandList.hdrColorTargetTransitions.size() == 2);
  REQUIRE(commandList.hdrColorTargetTransitions[0].target == &fakeHdr);
  REQUIRE(commandList.hdrColorTargetTransitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.hdrColorTargetTransitions[0].after == ResourceState::ColorAttachmentOutput);
  REQUIRE(commandList.hdrColorTargetTransitions[1].target == &fakeHdr);
  REQUIRE(commandList.hdrColorTargetTransitions[1].before == ResourceState::ColorAttachmentOutput);
  REQUIRE(commandList.hdrColorTargetTransitions[1].after == ResourceState::ShaderRead);

  // Dispatch correctness: the geometry pass used the HdrColorTarget-
  // shaped beginRendering() overload; the output-transform pass used
  // the ordinary RenderTarget-shaped one -- never crossed.
  REQUIRE(commandList.beginRenderingHdrCalls.size() == 1);
  REQUIRE(commandList.beginRenderingHdrCalls[0].color == &fakeHdr);
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  REQUIRE(commandList.beginRenderingCalls[0].color == &fakeFinal);

  // The output-transform pass's own real bindTexture(HdrColorTarget&,
  // Sampler&) call, recorded via the HdrColorTarget-shaped overload,
  // never the SampledTexture-shaped one.
  REQUIRE(commandList.boundHdrTextures.size() == 1);
  REQUIRE(commandList.boundHdrTextures[0].texture == &fakeHdr);
  REQUIRE(commandList.boundHdrTextures[0].sampler == &fakeSampler);
  REQUIRE(commandList.boundTextures.empty());

  // Pass order itself: execute() invokes every pass's own executeFn in
  // exactly the compiled order, geometry strictly before output-
  // transform -- derived automatically from the shared hdrResource's
  // own write-then-read declarations, never asserted here as a given.
  const auto geometryBeginIt =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::BeginRenderingHdr);
  const auto outputTransformBeginIt =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::BeginRendering);
  REQUIRE(geometryBeginIt != commandList.events.end());
  REQUIRE(outputTransformBeginIt != commandList.events.end());
  REQUIRE(geometryBeginIt < outputTransformBeginIt);
}

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

// Exercises Plan 0007 Section 6-7's ResourceBinding/execute() extension:
// Guard 0 (malformed binding), Guard 1 generalized to depth Texture
// bindings, Guard 2's scope staying target-only, draw-pass recognition
// (ColorAttachmentOutput/DepthAttachmentReadWrite, never
// ColorAttachmentWrite), beginRendering()/endRendering() wrapping a
// recognized draw pass's callback, and per-resource state tracking -- all
// with no Vulkan device anywhere in this test binary.

namespace {

using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::ResourceHandle;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeTexture;
using atlantis::rhi::ResourceState;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as execution_tests.cpp's own ScopedFailureHandler.
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

TEST_CASE("execute() rejects a ResourceBinding with neither target nor depthTexture set",
          "[render_graph][execution][attachment]") {
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
  // Neither target nor depthTexture set.
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() rejects a ResourceBinding with both target and depthTexture set",
          "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("both-target");
  FakeTexture fakeDepth("both-depth");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.target = &fakeTarget;
  malformed.depthTexture = &fakeDepth;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() rejects two bindings for the same resource", "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("dup");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTargetA("dup-a");
  FakeRenderTarget fakeTargetB("dup-b");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding bindingA{};
  bindingA.resource = compiled.value().resourceAt(0);
  bindingA.target = &fakeTargetA;
  ResourceBinding bindingB{};
  bindingB.resource = compiled.value().resourceAt(0);
  bindingB.target = &fakeTargetB;

  execute(compiled.value(), {bindingA, bindingB}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 1 generalizes to a depth Texture binding with no supplied entry",
          "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle depth = builder.declareResource("depth");
  const PassHandle pass = builder.declarePass("draw");
  builder.writes(pass, depth, ResourceState::DepthAttachmentReadWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  execute(compiled.value(), {}, commandList);  // no bindings supplied at all

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 2 fires for a target binding with a declared read usage",
          "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  builder.writes(producer, color, ResourceState::ColorAttachmentOutput);
  builder.reads(reader, color, ResourceState::ColorAttachmentOutput);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.target = &fakeColor;
  execute(compiled.value(), {binding}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 2 does not fire for an equivalent depthTexture binding with a declared read usage",
          "[render_graph][execution][attachment]") {
  // DepthAttachmentReadWrite's read-through-write encoding is expressed as
  // a single writes() usage (ADR-0018), never a separate reads() call, so
  // this test exercises Guard 2's scope directly: a depthTexture binding
  // to a resource that (unusually) also carries a declared *read* usage
  // must not itself trip Guard 2 -- only a target binding does.
  RenderGraphBuilder builder;
  const ResourceHandle depth = builder.declareResource("depth");
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  builder.writes(producer, depth, ResourceState::DepthAttachmentReadWrite);
  builder.reads(reader, depth);  // untagged read -- legal per Spec 0005, distinct from the write's own state tag

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeTexture fakeDepth("depth");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.depthTexture = &fakeDepth;
  execute(compiled.value(), {binding}, commandList);

  REQUIRE(failures.empty());
}

TEST_CASE("execute() recognizes a ColorAttachmentOutput/DepthAttachmentReadWrite pass as a draw pass",
          "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const ResourceHandle depth = builder.declareResource("depth");
  const PassHandle draw = builder.declarePass("draw");
  builder.writes(draw, color, ResourceState::ColorAttachmentOutput);
  builder.writes(draw, depth, ResourceState::DepthAttachmentReadWrite);

  std::vector<std::string> invoked;
  builder.setExecute(draw, [&invoked](atlantis::rhi::CommandList&) { invoked.push_back("draw"); });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeTexture fakeDepth("depth");
  FakeCommandList commandList;

  ResourceBinding colorBinding{};
  colorBinding.resource = compiled.value().resourceAt(0);
  colorBinding.target = &fakeColor;
  ResourceBinding depthBinding{};
  depthBinding.resource = compiled.value().resourceAt(1);
  depthBinding.depthTexture = &fakeDepth;

  execute(compiled.value(), {colorBinding, depthBinding}, commandList);

  REQUIRE(invoked == std::vector<std::string>{"draw"});
  REQUIRE(commandList.beginRenderingCalls.size() == 1);
  REQUIRE(commandList.beginRenderingCalls[0].color == &fakeColor);
  REQUIRE(commandList.beginRenderingCalls[0].depth == &fakeDepth);

  // beginRendering() immediately before, endRendering() immediately after,
  // the pass's own callback -- no other event in between.
  const auto beginIt =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::BeginRendering);
  REQUIRE(beginIt != commandList.events.end());
  const auto endIt =
      std::find(commandList.events.begin(), commandList.events.end(), FakeCommandList::EventKind::EndRendering);
  REQUIRE(endIt != commandList.events.end());
  REQUIRE(beginIt < endIt);
}

TEST_CASE("execute() never recognizes a ColorAttachmentWrite-only pass as a draw pass",
          "[render_graph][execution][attachment]") {
  // The concrete regression test for Section 7/ADR-0026's central fix:
  // Spec 0006's existing clearColor()-only pass must stay structurally
  // un-wrapped by the new attachment-scope rule.
  RenderGraphBuilder builder;
  const ResourceHandle target = builder.declareResource("target");
  const PassHandle clear = builder.declarePass("clear");
  builder.writes(clear, target, ResourceState::ColorAttachmentWrite);

  std::vector<std::string> invoked;
  builder.setExecute(clear, [&invoked](atlantis::rhi::CommandList&) { invoked.push_back("clear"); });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("target");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{{compiled.value().resourceAt(0), &fakeTarget}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(invoked == std::vector<std::string>{"clear"});
  REQUIRE(commandList.beginRenderingCalls.empty());
  REQUIRE(std::find(commandList.events.begin(), commandList.events.end(),
                     FakeCommandList::EventKind::BeginRendering) == commandList.events.end());
}

TEST_CASE("execute() tracks per-resource state independently across two simultaneously bound resources",
          "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const ResourceHandle depth = builder.declareResource("depth");
  const PassHandle draw = builder.declarePass("draw");
  builder.writes(draw, color, ResourceState::ColorAttachmentOutput);
  builder.writes(draw, depth, ResourceState::DepthAttachmentReadWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeTexture fakeDepth("depth");
  FakeCommandList commandList;

  ResourceBinding colorBinding{};
  colorBinding.resource = compiled.value().resourceAt(0);
  colorBinding.target = &fakeColor;
  ResourceBinding depthBinding{};
  depthBinding.resource = compiled.value().resourceAt(1);
  depthBinding.depthTexture = &fakeDepth;

  execute(compiled.value(), {colorBinding, depthBinding}, commandList);

  // Color: Undefined -> ColorAttachmentOutput, then trailing -> PresentSource.
  REQUIRE(commandList.transitions.size() == 2);
  REQUIRE(commandList.transitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.transitions[0].after == ResourceState::ColorAttachmentOutput);
  REQUIRE(commandList.transitions[1].after == ResourceState::PresentSource);

  // Depth: Undefined -> DepthAttachmentReadWrite, no trailing transition
  // (never presented, never read back this round).
  REQUIRE(commandList.textureTransitions.size() == 1);
  REQUIRE(commandList.textureTransitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.textureTransitions[0].after == ResourceState::DepthAttachmentReadWrite);
}

TEST_CASE("execute() starts every bound resource from Undefined on each independent call, including a second call",
          "[render_graph][execution][attachment]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle draw = builder.declarePass("draw");
  builder.writes(draw, color, ResourceState::ColorAttachmentOutput);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.target = &fakeColor;

  execute(compiled.value(), {binding}, commandList);
  execute(compiled.value(), {binding}, commandList);  // second, independent call

  REQUIRE(commandList.transitions.size() == 4);  // two calls, two transitions each
  // Second call's first transition must again begin from Undefined, not
  // continue from the first call's trailing PresentSource state.
  REQUIRE(commandList.transitions[2].before == ResourceState::Undefined);
  REQUIRE(commandList.transitions[2].after == ResourceState::ColorAttachmentOutput);
}

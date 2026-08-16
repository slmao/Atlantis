#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/types.h>

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fake_command_list.h"

// Exercises Spec 0010/ADR-0039's ResourceBinding::incomingState/finalState
// extension and execute()'s generalized transition-insertion algorithm --
// items 1-8 of the Plan's own Testing & Verification bullets, all
// GPU-independent (no Vulkan device anywhere in this test binary). Item 9
// (the copyRenderTargetToBuffer() callback-recording case) is added once
// FakeCommandList gains that override in a later implementation step.

namespace {

using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::ResourceHandle;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::rhi::ResourceState;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as execution_tests.cpp's/attachment_execution_tests.cpp's
// own ScopedFailureHandler.
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

TEST_CASE("execute() seeds a binding with no incomingState supplied from Undefined -- zero behavior change",
          "[render_graph][headless]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle pass = builder.declarePass("clear");
  builder.writes(pass, color, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{{.resource = compiled.value().resourceAt(0), .target = &fakeColor}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE_FALSE(commandList.transitions.empty());
  REQUIRE(commandList.transitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.transitions[0].after == ResourceState::ColorAttachmentWrite);
}

TEST_CASE("execute() seeds a binding with an explicit incomingState from that value instead of Undefined",
          "[render_graph][headless]") {
  // Models the headless copy pass's own precondition: the color target has
  // already been left in TransferSource by an earlier Renderer::drawFrame()
  // call within the same submission's recording, so the copy pass's own
  // binding must declare that as its incoming state, not Undefined.
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle copy = builder.declarePass("copy");
  builder.writes(copy, color, ResourceState::TransferSource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeColor, .incomingState = ResourceState::TransferSource}};

  execute(compiled.value(), bindings, commandList);

  // The one usage's declared state (TransferSource) equals the supplied
  // incomingState, so no per-usage transition is inserted at all -- and
  // finalState is std::nullopt (default), so no trailing transition
  // either. Zero transitions total.
  REQUIRE(commandList.transitions.empty());
}

TEST_CASE("execute() inserts a trailing transition to a supplied finalState when it differs from the ending state",
          "[render_graph][headless]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle pass = builder.declarePass("clear");
  builder.writes(pass, color, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeColor, .finalState = ResourceState::TransferSource}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.transitions.size() == 2);
  REQUIRE(commandList.transitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.transitions[0].after == ResourceState::ColorAttachmentWrite);
  REQUIRE(commandList.transitions[1].before == ResourceState::ColorAttachmentWrite);
  REQUIRE(commandList.transitions[1].after == ResourceState::TransferSource);
}

TEST_CASE("execute() inserts no trailing transition when finalState is std::nullopt", "[render_graph][headless]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle pass = builder.declarePass("clear");
  builder.writes(pass, color, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  // finalState left at its default (std::nullopt) -- no trailing transition
  // should be inserted, unlike every other test in this file that sets it.
  const std::vector<ResourceBinding> bindings{{.resource = compiled.value().resourceAt(0), .target = &fakeColor}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.transitions.size() == 1);  // only the per-usage transition, no trailing one
  REQUIRE(commandList.transitions[0].after == ResourceState::ColorAttachmentWrite);
}

TEST_CASE(
    "execute() inserts no transition at all when a resource's incomingState already equals its one usage's "
    "declared state",
    "[render_graph][headless]") {
  // The specific case the headless copy pass relies on: incomingState ==
  // the pass's own declared state means zero transitionResource() calls,
  // not even the per-usage one -- confirmed here independently of item 2's
  // own (differently-focused) assertion.
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle copy = builder.declarePass("copy");
  builder.writes(copy, color, ResourceState::TransferSource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeColor, .incomingState = ResourceState::TransferSource}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.transitions.empty());
}

TEST_CASE("execute() does not recognize a single TransferSource writes() usage as a draw pass",
          "[render_graph][headless]") {
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle copy = builder.declarePass("copy");
  builder.writes(copy, color, ResourceState::TransferSource);

  std::vector<std::string> invoked;
  builder.setExecute(copy, [&invoked](atlantis::rhi::CommandList&) { invoked.push_back("copy"); });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeColor("color");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeColor, .incomingState = ResourceState::TransferSource}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(invoked == std::vector<std::string>{"copy"});
  REQUIRE(commandList.beginRenderingCalls.empty());
}

TEST_CASE("Guard 1 and Guard 2 continue to hold for bindings that populate the new incomingState/finalState fields",
          "[render_graph][headless]") {
  // Guard 1: a ResourceState-tagged usage with no supplied binding still
  // fires, unaffected by the new fields (there is no binding at all here).
  RenderGraphBuilder builder;
  const ResourceHandle color = builder.declareResource("color");
  const PassHandle pass = builder.declarePass("copy");
  builder.writes(pass, color, ResourceState::TransferSource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  {
    ScopedFailureHandler handler(failures);
    execute(compiled.value(), {}, commandList);  // no bindings supplied
  }
  REQUIRE_FALSE(failures.empty());

  // Guard 2: a target binding with a declared read usage still fires, even
  // when incomingState/finalState are both populated.
  RenderGraphBuilder readerBuilder;
  const ResourceHandle readColor = readerBuilder.declareResource("read-color");
  const PassHandle producer = readerBuilder.declarePass("producer");
  const PassHandle reader = readerBuilder.declarePass("reader");
  readerBuilder.writes(producer, readColor, ResourceState::ColorAttachmentOutput);
  readerBuilder.reads(reader, readColor, ResourceState::ColorAttachmentOutput);

  const auto readCompiled = readerBuilder.compile();
  REQUIRE(readCompiled.isOk());

  FakeRenderTarget fakeReadColor("read-color");
  FakeCommandList readerCommandList;
  std::vector<RecordedFailure> readerFailures;
  ScopedFailureHandler readerHandler(readerFailures);

  const ResourceBinding binding{.resource = readCompiled.value().resourceAt(0),
                                 .target = &fakeReadColor,
                                 .incomingState = ResourceState::TransferSource,
                                 .finalState = ResourceState::TransferSource};
  execute(readCompiled.value(), {binding}, readerCommandList);

  REQUIRE_FALSE(readerFailures.empty());
}

TEST_CASE("execute() performs no transition on a resource never used by any pass, regardless of incomingState/finalState",
          "[render_graph][headless]") {
  RenderGraphBuilder builder;
  const ResourceHandle unused = builder.declareResource("unused");
  static_cast<void>(unused);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("unused");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{{.resource = compiled.value().resourceAt(0),
                                                .target = &fakeTarget,
                                                .incomingState = ResourceState::TransferSource,
                                                .finalState = ResourceState::PresentSource}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.transitions.empty());
}

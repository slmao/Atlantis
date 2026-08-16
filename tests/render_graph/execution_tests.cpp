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

// Exercises render_graph::execute()'s transition-insertion algorithm and
// both guard checks (Plan 0006 Section 7 / Spec 0006's own Testing &
// Verification Plan) through the full public RenderGraphBuilder/
// CompiledGraph/execute() API, with no Vulkan device anywhere in this
// test binary.

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

// Same RAII pattern as tests/render_graph/handle_ownership_tests.cpp's
// own ScopedFailureHandler -- installs a recording, non-terminating
// replacement failure handler for the lifetime of one test.
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

TEST_CASE("execute() inserts no transition when consecutive usages declare the same ResourceState",
          "[render_graph][execution]") {
  // A bound resource may carry a write producer but must never carry a
  // read usage (Guard 2) -- so a same-state adjacency cannot legally be
  // exercised as producer(write) -> reader(read) on a *bound* resource.
  // Declaring the same (pass, resource) write usage twice is legal and
  // idempotent (Spec 0005) and produces two usage records with an
  // identical ResourceState, without any read usage -- the correct way
  // to exercise a same-state adjacency here.
  RenderGraphBuilder builder;
  const ResourceHandle target = builder.declareResource("target");
  const PassHandle producer = builder.declarePass("producer");
  builder.writes(producer, target, ResourceState::ColorAttachmentWrite);
  builder.writes(producer, target, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("target");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeTarget, .finalState = ResourceState::PresentSource}};

  execute(compiled.value(), bindings, commandList);

  // Undefined -> ColorAttachmentWrite (first usage), then no transition
  // for the second, identically-tagged usage, then ColorAttachmentWrite
  // -> PresentSource (trailing).
  REQUIRE(commandList.transitions.size() == 2);
  REQUIRE(commandList.transitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.transitions[0].after == ResourceState::ColorAttachmentWrite);
  REQUIRE(commandList.transitions[1].before == ResourceState::ColorAttachmentWrite);
  REQUIRE(commandList.transitions[1].after == ResourceState::PresentSource);
}

TEST_CASE("execute() inserts exactly one transition when a resource's declared state changes",
          "[render_graph][execution]") {
  RenderGraphBuilder builder;
  const ResourceHandle target = builder.declareResource("target");
  const PassHandle pass = builder.declarePass("clear");
  builder.writes(pass, target, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("target");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeTarget, .finalState = ResourceState::PresentSource}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.transitions.size() == 2);  // Undefined->ColorAttachmentWrite, then trailing ->PresentSource
}

TEST_CASE("execute() inserts exactly one trailing transition regardless of pass count",
          "[render_graph][execution]") {
  // The bound resource itself may only ever carry a write producer, never
  // a read usage (Guard 2), so "regardless of pass count" is exercised
  // here as "regardless of how many *other*, unrelated passes exist in
  // the same graph" -- two extra passes that touch a second, unbound,
  // untagged resource, never the bound one.
  RenderGraphBuilder builder;
  const ResourceHandle target = builder.declareResource("target");
  const ResourceHandle other = builder.declareResource("other");
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle unrelatedA = builder.declarePass("unrelatedA");
  const PassHandle unrelatedB = builder.declarePass("unrelatedB");
  builder.writes(producer, target, ResourceState::ColorAttachmentWrite);
  builder.writes(unrelatedA, other);
  builder.reads(unrelatedB, other);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  REQUIRE(compiled.value().passCount() == 3);

  FakeRenderTarget fakeTarget("target");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0), .target = &fakeTarget, .finalState = ResourceState::PresentSource}};

  execute(compiled.value(), bindings, commandList);

  const auto trailingCount = std::count_if(commandList.transitions.begin(), commandList.transitions.end(),
                                            [](const auto& t) { return t.after == ResourceState::PresentSource; });
  REQUIRE(trailingCount == 1);
}

TEST_CASE("execute() performs no transition on a bound RenderTarget never used by any pass",
          "[render_graph][execution]") {
  RenderGraphBuilder builder;
  const ResourceHandle unused = builder.declareResource("unused");
  static_cast<void>(unused);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("unused");
  FakeCommandList commandList;
  const std::vector<ResourceBinding> bindings{{compiled.value().resourceAt(0), &fakeTarget}};

  execute(compiled.value(), bindings, commandList);

  REQUIRE(commandList.transitions.empty());
}

TEST_CASE("execute() invokes every pass's execution callback in exactly the compiled pass order",
          "[render_graph][execution]") {
  RenderGraphBuilder builder;
  const PassHandle first = builder.declarePass("first");
  const PassHandle second = builder.declarePass("second");

  std::vector<std::string> invoked;
  builder.setExecute(first, [&invoked](atlantis::rhi::CommandList&) { invoked.push_back("first"); });
  builder.setExecute(second, [&invoked](atlantis::rhi::CommandList&) { invoked.push_back("second"); });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  execute(compiled.value(), {}, commandList);

  REQUIRE(invoked == std::vector<std::string>{"first", "second"});
}

TEST_CASE("A plain untagged Spec 0005 usage compiles and executes with no transition and no assertion",
          "[render_graph][execution]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("token");
  const PassHandle pass = builder.declarePass("reads-only");
  builder.reads(pass, resource);  // untagged -- Spec 0005's own form

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  execute(compiled.value(), {}, commandList);

  REQUIRE(commandList.transitions.empty());
  REQUIRE(failures.empty());
}

TEST_CASE("execute() rejects a ResourceState-tagged usage with no supplied binding", "[render_graph][execution]") {
  RenderGraphBuilder builder;
  const ResourceHandle target = builder.declareResource("target");
  const PassHandle pass = builder.declarePass("clear");
  builder.writes(pass, target, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  execute(compiled.value(), {}, commandList);  // no bindings supplied

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() rejects binding a RenderTarget to a resource with a declared read usage",
          "[render_graph][execution]") {
  RenderGraphBuilder builder;
  const ResourceHandle target = builder.declareResource("target");
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  builder.writes(producer, target, ResourceState::ColorAttachmentWrite);
  builder.reads(reader, target, ResourceState::ColorAttachmentWrite);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("target");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  const std::vector<ResourceBinding> bindings{{compiled.value().resourceAt(0), &fakeTarget}};
  execute(compiled.value(), bindings, commandList);

  REQUIRE_FALSE(failures.empty());
}

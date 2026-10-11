#include <atlantis/render_graph/execution.h>

#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/types.h>

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fake_command_list.h"

// Spec 0059 R5 / ADR-0115 D4: render_graph::execute() brackets every pass in a
// named region -- automatically, in compiled order, around the pass's
// transitions, attachment scope and callback. No Vulkan device in this binary.
// The regions are emitted when ATLANTIS_PROFILING or ATLANTIS_GPU_MARKERS is
// on (the Windows default); a build with both off emits none.

namespace {

using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::ResourceHandle;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeHdrColorTarget;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeTexture;
using atlantis::rhi::ResourceState;
using EventKind = FakeCommandList::EventKind;

#if defined(ATLANTIS_PROFILING) || defined(ATLANTIS_GPU_MARKERS)
constexpr bool kRegionsExpected = true;
#else
constexpr bool kRegionsExpected = false;
#endif

[[nodiscard]] std::vector<std::string> beginNames(const FakeCommandList& list) {
  std::vector<std::string> names;
  for (const auto& event : list.regionLog) {
    if (event.begin) names.push_back(event.name);
  }
  return names;
}

}  // namespace

TEST_CASE("every pass is bracketed by a balanced region named after its label, in compiled order",
          "[render_graph][execution][region]") {
  RenderGraphBuilder builder;
  const ResourceHandle hdr = builder.declareResource("hdr");
  const ResourceHandle depth = builder.declareResource("depth");
  const ResourceHandle final = builder.declareResource("final");
  // Declared out of dependency order on purpose: compile() decides the order.
  const PassHandle present = builder.declarePass("output_transform");
  const PassHandle draw = builder.declarePass("draw");
  builder.writes(draw, hdr, ResourceState::ColorAttachmentOutput);
  builder.writes(draw, depth, ResourceState::DepthAttachmentReadWrite);
  builder.reads(present, hdr, ResourceState::ShaderRead);
  builder.writes(present, final, ResourceState::ColorAttachmentOutput);
  int calls = 0;
  builder.setExecute(draw, [&calls](atlantis::rhi::CommandList&) { ++calls; });
  builder.setExecute(present, [&calls](atlantis::rhi::CommandList&) { ++calls; });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeHdrColorTarget fakeHdr("hdr");
  FakeTexture fakeDepth("depth");
  FakeRenderTarget fakeFinal("final");
  FakeCommandList commandList;
  std::vector<ResourceBinding> bindings(3);
  bindings[0].resource = compiled.value().resourceAt(0);
  bindings[0].hdrColorTarget = &fakeHdr;
  bindings[1].resource = compiled.value().resourceAt(1);
  bindings[1].depthTexture = &fakeDepth;
  bindings[2].resource = compiled.value().resourceAt(2);
  bindings[2].target = &fakeFinal;
  execute(compiled.value(), bindings, commandList);
  CHECK(calls == 2);

  if (!kRegionsExpected) {
    CHECK(commandList.regionLog.empty());
    return;
  }
  REQUIRE(commandList.regionLog.size() == 4);
  CHECK(beginNames(commandList) == std::vector<std::string>{"draw", "output_transform"});  // dependency order
  CHECK(commandList.regionLog[0].begin);
  CHECK_FALSE(commandList.regionLog[1].begin);
  CHECK(commandList.regionLog[2].begin);
  CHECK_FALSE(commandList.regionLog[3].begin);

  // The first region opens before the pass's first command (its transitions)
  // and closes after endRendering(); the second opens only after the first
  // closed.
  CHECK(commandList.regionLog[0].eventIndex == 0);
  REQUIRE_FALSE(commandList.events.empty());
  CHECK(commandList.regionLog[1].eventIndex > 0);
  CHECK(commandList.events[commandList.regionLog[1].eventIndex - 1] == EventKind::EndRendering);
  CHECK(commandList.regionLog[2].eventIndex >= commandList.regionLog[1].eventIndex);
  CHECK(commandList.regionLog[3].eventIndex == commandList.events.size());
}

TEST_CASE("a pass with no label gets a positional fallback name", "[render_graph][execution][region]") {
  RenderGraphBuilder builder;
  const PassHandle unlabeled = builder.declarePass();
  static_cast<void>(unlabeled);
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  execute(compiled.value(), {}, commandList);

  if (!kRegionsExpected) {
    CHECK(commandList.regionLog.empty());
    return;
  }
  CHECK(beginNames(commandList) == std::vector<std::string>{"pass_0"});
  CHECK(commandList.regionLog.size() == 2);
}

TEST_CASE("a graph with no passes emits no region", "[render_graph][execution][region]") {
  RenderGraphBuilder builder;
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  FakeCommandList commandList;
  execute(compiled.value(), {}, commandList);
  CHECK(commandList.regionLog.empty());
}

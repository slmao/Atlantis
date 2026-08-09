#include <atlantis/render_graph/compile_error.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Exercises cycle detection through the full public RenderGraphBuilder/
// CompiledGraph API (Plan 0005 Section 9) -- every fixture here derives
// its cycle entirely from producer-derived edges (Read/Write usages), the
// only kind of edge this module ever derives.

namespace {

using atlantis::render_graph::DependencyCycleError;
using atlantis::render_graph::MultipleProducersError;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceHandle;

TEST_CASE("A two-pass, two-resource cycle is reported through the public API", "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  builder.writes(a, r0);
  builder.reads(a, r1);
  builder.writes(b, r1);
  builder.reads(b, r0);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<DependencyCycleError>(result.error()));
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 2);
  REQUIRE(error.passes[0].declarationIndex == 0);
  REQUIRE(error.passes[1].declarationIndex == 1);
}

TEST_CASE("A longer, three-pass cycle is reported through the public API", "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const PassHandle c = builder.declarePass("C");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  const ResourceHandle r2 = builder.declareResource("r2");
  builder.writes(a, r0);
  builder.reads(a, r2);
  builder.writes(b, r1);
  builder.reads(b, r0);
  builder.writes(c, r2);
  builder.reads(c, r1);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 3);
  REQUIRE(error.passes[0].declarationIndex == 0);
  REQUIRE(error.passes[1].declarationIndex == 1);
  REQUIRE(error.passes[2].declarationIndex == 2);
}

TEST_CASE("A downstream pass with a smaller declarationIndex than any cycle member is excluded "
          "from the reported witness",
          "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle downstream = builder.declarePass("downstream");
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  const ResourceHandle r2 = builder.declareResource("r2");
  builder.reads(downstream, r2);
  builder.writes(a, r0);
  builder.reads(a, r1);
  builder.writes(a, r2);
  builder.writes(b, r1);
  builder.reads(b, r0);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 2);
  for (const auto& diagnostic : error.passes) {
    REQUIRE(diagnostic.declarationIndex != 0);
  }
}

TEST_CASE("When more than one independent cycle exists, the same one is reported deterministically "
          "through the public API",
          "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const PassHandle c = builder.declarePass("C");
  const PassHandle d = builder.declarePass("D");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  const ResourceHandle r2 = builder.declareResource("r2");
  const ResourceHandle r3 = builder.declareResource("r3");
  builder.writes(a, r0);
  builder.reads(a, r1);
  builder.writes(b, r1);
  builder.reads(b, r0);
  builder.writes(c, r2);
  builder.reads(c, r3);
  builder.writes(d, r3);
  builder.reads(d, r2);

  const auto first = builder.compile();
  const auto second = builder.compile();

  REQUIRE(first.isErr());
  REQUIRE(second.isErr());
  const DependencyCycleError& firstError = std::get<DependencyCycleError>(first.error());
  const DependencyCycleError& secondError = std::get<DependencyCycleError>(second.error());
  REQUIRE(firstError.passes.size() == 2);
  REQUIRE(firstError.passes[0].declarationIndex == 0);
  REQUIRE(firstError.passes[1].declarationIndex == 1);
  REQUIRE(secondError.passes.size() == firstError.passes.size());
  REQUIRE(secondError.passes[0].declarationIndex == firstError.passes[0].declarationIndex);
  REQUIRE(secondError.passes[1].declarationIndex == firstError.passes[1].declarationIndex);
}

TEST_CASE("A cycle whose passes carry duplicate labels is still disambiguated by declarationIndex "
          "through the public API",
          "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("dup");
  const PassHandle b = builder.declarePass("dup");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  builder.writes(a, r0);
  builder.reads(a, r1);
  builder.writes(b, r1);
  builder.reads(b, r0);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 2);
  REQUIRE(error.passes[0].label == "dup");
  REQUIRE(error.passes[1].label == "dup");
  REQUIRE(error.passes[0].declarationIndex != error.passes[1].declarationIndex);
}

TEST_CASE("Every adjacent pair in a reported witness, including the wrap-around pair, is a real derived edge",
          "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const PassHandle c = builder.declarePass("C");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  const ResourceHandle r2 = builder.declareResource("r2");
  builder.writes(a, r0);
  builder.reads(a, r2);
  builder.writes(b, r1);
  builder.reads(b, r0);
  builder.writes(c, r2);
  builder.reads(c, r1);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 3);

  // This fixture's only derived edges are 0->1, 1->2, 2->0 (each pass
  // reads the resource produced by the pass one index lower, wrapping
  // around) -- the reported witness must trace exactly that cycle.
  auto isRealEdge = [](std::size_t from, std::size_t to) {
    return (from == 0 && to == 1) || (from == 1 && to == 2) || (from == 2 && to == 0);
  };
  for (std::size_t i = 0; i < error.passes.size(); ++i) {
    const std::size_t from = error.passes[i].declarationIndex;
    const std::size_t to = error.passes[(i + 1) % error.passes.size()].declarationIndex;
    REQUIRE(isRealEdge(from, to));
  }
}

TEST_CASE("Repeated compilation of the same cyclic graph reports an equivalent cycle payload",
          "[render_graph][cycle_detection]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  builder.writes(a, r0);
  builder.reads(a, r1);
  builder.writes(b, r1);
  builder.reads(b, r0);

  const auto first = builder.compile();
  const auto second = builder.compile();

  REQUIRE(first.isErr());
  REQUIRE(second.isErr());
  const DependencyCycleError& firstError = std::get<DependencyCycleError>(first.error());
  const DependencyCycleError& secondError = std::get<DependencyCycleError>(second.error());
  REQUIRE(firstError.passes.size() == secondError.passes.size());
  for (std::size_t i = 0; i < firstError.passes.size(); ++i) {
    REQUIRE(firstError.passes[i].declarationIndex == secondError.passes[i].declarationIndex);
    REQUIRE(firstError.passes[i].label == secondError.passes[i].label);
  }
}

}  // namespace

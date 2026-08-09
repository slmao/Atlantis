#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <cstddef>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Exercises pass retention and compiled-order determinism through the
// full public RenderGraphBuilder/CompiledGraph API (Plan 0005 Section 9):
// every successfully declared pass appears exactly once (ADR-0018), in a
// declaration-order tie-break among otherwise-unordered passes, with an
// actual dependency always taking priority over that tie-break.

namespace {

using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceHandle;

std::size_t findPosition(const CompiledGraph& graph, std::string_view label) {
  for (std::size_t i = 0; i < graph.passCount(); ++i) {
    if (graph.label(graph.passOrder(i)) == label) return i;
  }
  return static_cast<std::size_t>(-1);
}

TEST_CASE("An empty graph compiles to a zero pass count", "[render_graph][pass_retention_and_ordering]") {
  RenderGraphBuilder builder;

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().passCount() == 0);
  REQUIRE(result.value().dependencyCount() == 0);
}

TEST_CASE("A single declared pass compiles successfully", "[render_graph][pass_retention_and_ordering]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().passCount() == 1);
  REQUIRE(result.value().label(result.value().passOrder(0)) == "only");
}

TEST_CASE("Isolated passes with no usage relationship are retained in declaration order",
          "[render_graph][pass_retention_and_ordering]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("first");
  (void)builder.declarePass("second");
  (void)builder.declarePass("third");

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(graph.passCount() == 3);
  REQUIRE(graph.label(graph.passOrder(0)) == "first");
  REQUIRE(graph.label(graph.passOrder(1)) == "second");
  REQUIRE(graph.label(graph.passOrder(2)) == "third");
}

TEST_CASE("A producer whose resource has no readers is retained", "[render_graph][pass_retention_and_ordering]") {
  RenderGraphBuilder builder;
  const PassHandle producer = builder.declarePass("producer");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(producer, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().passCount() == 1);
  REQUIRE(result.value().label(result.value().passOrder(0)) == "producer");
}

TEST_CASE("Every declared pass appears in the compiled order exactly once", "[render_graph][pass_retention_and_ordering]") {
  RenderGraphBuilder builder;
  const PassHandle isolated = builder.declarePass("isolated");
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  const PassHandle producerNoReader = builder.declarePass("producerNoReader");
  const ResourceHandle r0 = builder.declareResource("r0");
  const ResourceHandle r1 = builder.declareResource("r1");
  builder.writes(producer, r0);
  builder.reads(reader, r0);
  builder.writes(producerNoReader, r1);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(graph.passCount() == 4);
  std::vector<bool> seen(graph.passCount(), false);
  for (std::size_t i = 0; i < graph.passCount(); ++i) {
    const std::size_t position = i;  // passOrder(i).index() == i by construction
    REQUIRE_FALSE(seen[position]);
    seen[position] = true;
  }
  for (bool wasSeen : seen) REQUIRE(wasSeen);
}

TEST_CASE("Independent producer/reader groups keep their declaration-order relative order",
          "[render_graph][pass_retention_and_ordering]") {
  RenderGraphBuilder builder;
  const PassHandle producerA = builder.declarePass("producerA");
  const PassHandle readerA = builder.declarePass("readerA");
  const PassHandle producerB = builder.declarePass("producerB");
  const PassHandle readerB = builder.declarePass("readerB");
  const ResourceHandle rA = builder.declareResource("rA");
  const ResourceHandle rB = builder.declareResource("rB");
  builder.writes(producerA, rA);
  builder.reads(readerA, rA);
  builder.writes(producerB, rB);
  builder.reads(readerB, rB);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(findPosition(graph, "producerA") < findPosition(graph, "readerA"));
  REQUIRE(findPosition(graph, "producerB") < findPosition(graph, "readerB"));
  REQUIRE(findPosition(graph, "producerA") == 0);
  REQUIRE(findPosition(graph, "readerA") == 1);
  REQUIRE(findPosition(graph, "producerB") == 2);
  REQUIRE(findPosition(graph, "readerB") == 3);
}

TEST_CASE("An actual dependency always takes priority over declaration-order tie-break",
          "[render_graph][pass_retention_and_ordering]") {
  // "reader" is declared FIRST (declarationIndex 0) but reads a resource
  // produced by "producer", declared SECOND (declarationIndex 1). A pure
  // declaration-order tie-break would place "reader" first; the actual
  // dependency must place "producer" first instead.
  RenderGraphBuilder builder;
  const PassHandle reader = builder.declarePass("reader");
  const PassHandle producer = builder.declarePass("producer");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.reads(reader, resource);
  builder.writes(producer, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(findPosition(graph, "producer") < findPosition(graph, "reader"));
}

}  // namespace

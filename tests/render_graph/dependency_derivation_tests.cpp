#include <atlantis/assert.h>
#include <atlantis/render_graph/compile_error.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// This file covers the same-pass usage-conflict and duplicate-usage
// legality cases reachable through RenderGraphBuilder::reads()/writes()
// (Plan 0005 Section 7 items 3-4), plus the producer/reader
// edge-derivation, fan-out, and multiple-producer cases -- exercised
// through the full public RenderGraphBuilder/CompiledGraph API, now that
// RenderGraphBuilder::compile() is implemented (Plan 0005 Section 9).

namespace {

using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::CompiledPassId;
using atlantis::render_graph::MultipleProducersError;
using atlantis::render_graph::DependencyCycleError;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceHandle;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// See tests/render_graph/handle_ownership_tests.cpp for the same pattern
// with an identical rationale; duplicated locally rather than shared
// through a new header, since this implementation step's authorized file
// list does not include one.
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

TEST_CASE("A pass reading then writing the same resource triggers the assertion policy",
          "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.reads(pass, resource);
  builder.writes(pass, resource);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("A pass writing then reading the same resource triggers the assertion policy",
          "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.writes(pass, resource);
  builder.reads(pass, resource);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("Declaring the same read usage twice on the same pass is legal", "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.reads(pass, resource);
  builder.reads(pass, resource);

  REQUIRE(recorded.empty());
}

TEST_CASE("Declaring the same write usage twice on the same pass is legal", "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.writes(pass, resource);
  builder.writes(pass, resource);

  REQUIRE(recorded.empty());
}

// Returns the compiled position of the pass whose label is `label`, or
// the invalid sentinel if none matches -- labels are unique in every
// fixture below, so this is an unambiguous lookup.
std::size_t findPosition(const CompiledGraph& graph, std::string_view label) {
  for (std::size_t i = 0; i < graph.passCount(); ++i) {
    if (graph.label(graph.passOrder(i)) == label) return i;
  }
  return static_cast<std::size_t>(-1);
}

TEST_CASE("A producer with a single reader produces one correctly-ordered dependency edge",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(producer, resource);
  builder.reads(reader, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(graph.passCount() == 2);
  REQUIRE(graph.dependencyCount() == 1);
  const std::size_t producerPos = findPosition(graph, "producer");
  const std::size_t readerPos = findPosition(graph, "reader");
  REQUIRE(graph.dependency(0).from == graph.passOrder(producerPos));
  REQUIRE(graph.dependency(0).to == graph.passOrder(readerPos));
}

TEST_CASE("A producer with multiple readers fans out with no edge between the readers",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle readerA = builder.declarePass("readerA");
  const PassHandle readerB = builder.declarePass("readerB");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(producer, resource);
  builder.reads(readerA, resource);
  builder.reads(readerB, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(graph.dependencyCount() == 2);
  const CompiledPassId producerId = graph.passOrder(findPosition(graph, "producer"));
  const CompiledPassId readerAId = graph.passOrder(findPosition(graph, "readerA"));
  const CompiledPassId readerBId = graph.passOrder(findPosition(graph, "readerB"));
  for (std::size_t i = 0; i < graph.dependencyCount(); ++i) {
    REQUIRE(graph.dependency(i).from == producerId);
    REQUIRE_FALSE((graph.dependency(i).from == readerAId && graph.dependency(i).to == readerBId));
    REQUIRE_FALSE((graph.dependency(i).from == readerBId && graph.dependency(i).to == readerAId));
  }
}

TEST_CASE("Two readers of the same producer-less resource produce no edge between them (read-after-read)",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle readerA = builder.declarePass("readerA");
  const PassHandle readerB = builder.declarePass("readerB");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.reads(readerA, resource);
  builder.reads(readerB, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().dependencyCount() == 0);
}

TEST_CASE("A producer-less resource is legal and its reader produces no edge",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle reader = builder.declarePass("reader");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.reads(reader, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().passCount() == 1);
  REQUIRE(result.value().dependencyCount() == 0);
}

TEST_CASE("Declaring the same read usage twice through the public API still produces exactly one edge",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(producer, resource);
  builder.reads(reader, resource);
  builder.reads(reader, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().dependencyCount() == 1);
}

TEST_CASE("Declaring the same write usage twice through the public API does not count as a second producer",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(producer, resource);
  builder.writes(producer, resource);
  builder.reads(reader, resource);

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().dependencyCount() == 1);
}

TEST_CASE("More than one producer of a resource is reported through the public API",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const ResourceHandle resource = builder.declareResource("targetRes");
  builder.writes(a, resource);
  builder.writes(b, resource);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<MultipleProducersError>(result.error()));
  const MultipleProducersError& error = std::get<MultipleProducersError>(result.error());
  REQUIRE(error.resource.declarationIndex == 0);
  REQUIRE(error.resource.label == "targetRes");
  REQUIRE(error.producers.size() == 2);
  REQUIRE(error.producers[0].label == "A");
  REQUIRE(error.producers[1].label == "B");
}

TEST_CASE("When two resources both have multiple producers, the smaller declarationIndex one is reported "
          "through the public API",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const PassHandle c = builder.declarePass("C");
  const PassHandle d = builder.declarePass("D");
  const ResourceHandle first = builder.declareResource("first");
  const ResourceHandle second = builder.declareResource("second");
  builder.writes(a, first);
  builder.writes(b, first);
  builder.writes(c, second);
  builder.writes(d, second);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  const MultipleProducersError& error = std::get<MultipleProducersError>(result.error());
  REQUIRE(error.resource.declarationIndex == 0);
  REQUIRE(error.resource.label == "first");
}

TEST_CASE("A multiple-producer conflict is reported through the public API even when an unrelated cycle exists",
          "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const PassHandle c = builder.declarePass("C");
  const PassHandle d = builder.declarePass("D");
  const ResourceHandle conflicted = builder.declareResource("conflicted");
  const ResourceHandle r1 = builder.declareResource("r1");
  const ResourceHandle r2 = builder.declareResource("r2");
  builder.writes(a, conflicted);
  builder.writes(b, conflicted);
  builder.writes(c, r1);
  builder.reads(c, r2);
  builder.writes(d, r2);
  builder.reads(d, r1);

  const auto result = builder.compile();

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<MultipleProducersError>(result.error()));
  REQUIRE_FALSE(std::holds_alternative<DependencyCycleError>(result.error()));
}

TEST_CASE("A declared-but-unused resource is legal", "[render_graph][dependency_derivation]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");
  (void)builder.declareResource("unused");

  const auto result = builder.compile();

  REQUIRE(result.isOk());
  REQUIRE(result.value().passCount() == 1);
  REQUIRE(result.value().dependencyCount() == 0);
}

}  // namespace

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

// Exercises CompiledGraph/RenderGraphBuilder ownership and lifetime
// guarantees through the full public API (Plan 0005 Section 9).
//
// Explicitly NOT covered here, per Plan 0005 Section 5/8/9: using a
// CompiledPassId against a CompiledGraph other than the one that vended
// it (an undetected, graph-scoped identity precondition violation when
// its index happens to be in range for the other graph -- not something
// this module tests dynamically), and using a handle/CompiledGraph view
// after the lifetime rule that invalidates it (builder destruction for a
// handle; move-from/move-assignment-destination/destruction for a
// label() view) -- no test below dereferences a std::string_view once
// its own CompiledGraph has been moved from or destroyed.

namespace {

using atlantis::render_graph::CompiledDependencyEdge;
using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::CompiledPassId;
using atlantis::render_graph::MultipleProducersError;
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

TEST_CASE("A CompiledGraph remains fully queryable after its originating builder is destroyed",
          "[render_graph][ownership_lifetime]") {
  CompiledGraph graph = [] {
    RenderGraphBuilder builder;
    const PassHandle producer = builder.declarePass("producer");
    const PassHandle reader = builder.declarePass("reader");
    const ResourceHandle resource = builder.declareResource("r0");
    builder.writes(producer, resource);
    builder.reads(reader, resource);
    auto result = builder.compile();
    return std::move(result.value());
  }();  // the builder above is destroyed here, before `graph` is queried

  REQUIRE(graph.passCount() == 2);
  REQUIRE(graph.dependencyCount() == 1);
  REQUIRE(graph.label(graph.passOrder(0)) == "producer");
  REQUIRE(graph.label(graph.passOrder(1)) == "reader");
}

TEST_CASE("Further declarations on the builder do not change an already-produced CompiledGraph",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("first");
  auto result = builder.compile();
  REQUIRE(result.isOk());
  CompiledGraph graph = std::move(result.value());

  (void)builder.declarePass("second");  // added after compile()

  REQUIRE(graph.passCount() == 1);
  REQUIRE(graph.label(graph.passOrder(0)) == "first");
}

TEST_CASE("Two compiles on the same unmodified builder produce independent, equivalent CompiledGraph values",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  const PassHandle producer = builder.declarePass("producer");
  const PassHandle reader = builder.declarePass("reader");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(producer, resource);
  builder.reads(reader, resource);

  auto first = builder.compile();
  auto second = builder.compile();

  REQUIRE(first.isOk());
  REQUIRE(second.isOk());
  REQUIRE(first.value().passCount() == second.value().passCount());
  for (std::size_t i = 0; i < first.value().passCount(); ++i) {
    REQUIRE(first.value().label(first.value().passOrder(i)) == second.value().label(second.value().passOrder(i)));
  }
  REQUIRE(first.value().dependencyCount() == second.value().dependencyCount());
}

TEST_CASE("Destroying one CompiledGraph does not affect another independently produced one",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");

  auto first = builder.compile();
  REQUIRE(first.isOk());
  CompiledGraph graphA = std::move(first.value());

  {
    auto second = builder.compile();
    REQUIRE(second.isOk());
    CompiledGraph graphB = std::move(second.value());
    REQUIRE(graphB.passCount() == 1);
  }  // graphB destroyed here

  REQUIRE(graphA.passCount() == 1);
  REQUIRE(graphA.label(graphA.passOrder(0)) == "only");
}

TEST_CASE("A failed compile does not invalidate the builder, and repeated compile yields an equivalent error",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  const PassHandle a = builder.declarePass("A");
  const PassHandle b = builder.declarePass("B");
  const ResourceHandle resource = builder.declareResource("r0");
  builder.writes(a, resource);
  builder.writes(b, resource);

  auto first = builder.compile();
  auto second = builder.compile();

  REQUIRE(first.isErr());
  REQUIRE(second.isErr());
  const MultipleProducersError& firstError = std::get<MultipleProducersError>(first.error());
  const MultipleProducersError& secondError = std::get<MultipleProducersError>(second.error());
  REQUIRE(firstError.resource.declarationIndex == secondError.resource.declarationIndex);
  REQUIRE(firstError.resource.label == secondError.resource.label);
  REQUIRE(firstError.producers.size() == secondError.producers.size());
}

TEST_CASE("Duplicate diagnostic labels are legal and do not affect identity or order",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("dup");
  (void)builder.declarePass("dup");
  (void)builder.declareResource("dup");

  auto result = builder.compile();

  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();
  REQUIRE(graph.passCount() == 2);
  REQUIRE(graph.label(graph.passOrder(0)) == "dup");
  REQUIRE(graph.label(graph.passOrder(1)) == "dup");
  REQUIRE_FALSE(graph.passOrder(0) == graph.passOrder(1));
}

TEST_CASE("CompiledGraph supports move construction", "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");
  auto result = builder.compile();
  REQUIRE(result.isOk());

  CompiledGraph moved(std::move(result.value()));

  REQUIRE(moved.passCount() == 1);
  REQUIRE(moved.label(moved.passOrder(0)) == "only");
}

TEST_CASE("CompiledGraph supports move assignment", "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builderA;
  (void)builderA.declarePass("fromA");
  RenderGraphBuilder builderB;
  (void)builderB.declarePass("fromB");

  auto resultA = builderA.compile();
  auto resultB = builderB.compile();
  REQUIRE(resultA.isOk());
  REQUIRE(resultB.isOk());

  CompiledGraph graph = std::move(resultA.value());
  graph = std::move(resultB.value());

  REQUIRE(graph.passCount() == 1);
  REQUIRE(graph.label(graph.passOrder(0)) == "fromB");
}

TEST_CASE("passOrder() with an out-of-range position triggers the assertion policy and returns the invalid sentinel",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");
  auto result = builder.compile();
  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();

  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  const CompiledPassId id = graph.passOrder(5);  // passCount() == 1, so 5 is out of range

  REQUIRE(recorded.size() == 1);
  REQUIRE(id == CompiledPassId{});
}

TEST_CASE("label() with a default-constructed CompiledPassId triggers the assertion policy "
          "and returns an empty view",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");
  auto result = builder.compile();
  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();

  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  const std::string_view label = graph.label(CompiledPassId{});

  REQUIRE(recorded.size() == 1);
  REQUIRE(label.empty());
}

TEST_CASE("label() with an out-of-range CompiledPassId (obtained from this same graph's own "
          "out-of-range passOrder()) triggers the assertion policy and returns an empty view",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");
  auto result = builder.compile();
  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();

  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  const CompiledPassId outOfRange = graph.passOrder(5);  // already asserts once; recorded.size() becomes 1
  const std::string_view label = graph.label(outOfRange);  // asserts a second time

  REQUIRE(recorded.size() == 2);
  REQUIRE(label.empty());
}

TEST_CASE("dependency() with an out-of-range index triggers the assertion policy "
          "and returns an edge with both endpoints invalid",
          "[render_graph][ownership_lifetime]") {
  RenderGraphBuilder builder;
  (void)builder.declarePass("only");
  auto result = builder.compile();
  REQUIRE(result.isOk());
  const CompiledGraph& graph = result.value();

  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  const CompiledDependencyEdge edge = graph.dependency(5);  // dependencyCount() == 0, so 5 is out of range

  REQUIRE(recorded.size() == 1);
  REQUIRE(edge.from == CompiledPassId{});
  REQUIRE(edge.to == CompiledPassId{});
}

}  // namespace

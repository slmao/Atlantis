#include "compile_algorithm.h"

#include <atlantis/render_graph/compile_error.h>

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// White-box tests for atlantis::render_graph::detail::compile(), exercised
// directly against hand-built RawPass/RawResource fixtures -- no
// RenderGraphBuilder involved (Plan 0005 Section 9). Every fixture here
// respects this algorithm's own stated precondition (compile_algorithm.h):
// valid resource indices, and no single pass reading and writing the same
// resource -- that combination is rejected by RenderGraphBuilder before a
// usage is ever recorded, so it is never constructed here.

namespace {

using atlantis::render_graph::DependencyCycleError;
using atlantis::render_graph::MultipleProducersError;
using atlantis::render_graph::PassDiagnostic;
using atlantis::render_graph::detail::CompiledGraphData;
using atlantis::render_graph::detail::RawPass;
using atlantis::render_graph::detail::RawResource;
using atlantis::render_graph::detail::RawUsage;
using atlantis::render_graph::detail::UsageKind;
using atlantis::render_graph::detail::compile;

RawPass pass(std::string label, std::vector<RawUsage> usages = {}) {
  return RawPass{std::move(label), std::move(usages)};
}

RawResource resource(std::string label) { return RawResource{std::move(label)}; }

TEST_CASE("An empty graph compiles successfully with no passes and no edges", "[render_graph][compile_algorithm]") {
  const auto result = compile({}, {});

  REQUIRE(result.isOk());
  REQUIRE(result.value().passesInOrder.empty());
  REQUIRE(result.value().edges.empty());
}

TEST_CASE("A single isolated pass compiles successfully", "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {pass("only")};

  const auto result = compile(passes, {});

  REQUIRE(result.isOk());
  REQUIRE(result.value().passesInOrder.size() == 1);
  REQUIRE(result.value().passesInOrder[0].label == "only");
  REQUIRE(result.value().edges.empty());
}

TEST_CASE("A producer with a single reader produces one correctly-ordered edge",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("producer", {RawUsage{0, UsageKind::Write}}),
      pass("reader", {RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  const CompiledGraphData& data = result.value();
  REQUIRE(data.passesInOrder.size() == 2);
  REQUIRE(data.passesInOrder[0].label == "producer");
  REQUIRE(data.passesInOrder[1].label == "reader");
  REQUIRE(data.edges.size() == 1);
  REQUIRE(data.edges[0].from == 0);
  REQUIRE(data.edges[0].to == 1);
}

TEST_CASE("A producer with multiple readers fans out with no edge between the readers",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("producer", {RawUsage{0, UsageKind::Write}}),
      pass("readerA", {RawUsage{0, UsageKind::Read}}),
      pass("readerB", {RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  const CompiledGraphData& data = result.value();
  REQUIRE(data.edges.size() == 2);
  for (const auto& edge : data.edges) {
    REQUIRE(edge.from == 0);
    REQUIRE(edge.to != edge.from);
  }
  // No edge directly between the two readers (positions 1 and 2, in
  // either direction) -- only producer -> reader edges are ever derived.
  for (const auto& edge : data.edges) {
    REQUIRE_FALSE((edge.from == 1 && edge.to == 2));
    REQUIRE_FALSE((edge.from == 2 && edge.to == 1));
  }
}

TEST_CASE("A producer-less resource is legal and its reader produces no edge", "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {pass("reader", {RawUsage{0, UsageKind::Read}})};
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  REQUIRE(result.value().passesInOrder.size() == 1);
  REQUIRE(result.value().edges.empty());
}

TEST_CASE("Two readers of the same producer-less resource produce no edge between them (read-after-read)",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("readerA", {RawUsage{0, UsageKind::Read}}),
      pass("readerB", {RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  REQUIRE(result.value().edges.empty());
}

TEST_CASE("Declaring the same read usage twice on one pass still produces exactly one edge",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("producer", {RawUsage{0, UsageKind::Write}}),
      pass("reader", {RawUsage{0, UsageKind::Read}, RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  REQUIRE(result.value().edges.size() == 1);
}

TEST_CASE("Declaring the same write usage twice on one pass does not count as a second producer",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("producer", {RawUsage{0, UsageKind::Write}, RawUsage{0, UsageKind::Write}}),
      pass("reader", {RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  REQUIRE(result.value().edges.size() == 1);
}

TEST_CASE("More than one producer of a resource is reported with the resource's identity and its producers",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}}),
      pass("B", {RawUsage{0, UsageKind::Write}}),
  };
  const std::vector<RawResource> resources = {resource("targetRes")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<MultipleProducersError>(result.error()));
  const MultipleProducersError& error = std::get<MultipleProducersError>(result.error());
  REQUIRE(error.resource.declarationIndex == 0);
  REQUIRE(error.resource.label == "targetRes");
  REQUIRE(error.producers.size() == 2);
  REQUIRE(error.producers[0].declarationIndex == 0);
  REQUIRE(error.producers[0].label == "A");
  REQUIRE(error.producers[1].declarationIndex == 1);
  REQUIRE(error.producers[1].label == "B");
}

TEST_CASE("When two resources both have multiple producers, the smaller declarationIndex one is reported",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}}),
      pass("B", {RawUsage{0, UsageKind::Write}}),
      pass("C", {RawUsage{1, UsageKind::Write}}),
      pass("D", {RawUsage{1, UsageKind::Write}}),
  };
  const std::vector<RawResource> resources = {resource("first"), resource("second")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<MultipleProducersError>(result.error()));
  REQUIRE(std::get<MultipleProducersError>(result.error()).resource.declarationIndex == 0);
  REQUIRE(std::get<MultipleProducersError>(result.error()).resource.label == "first");
}

TEST_CASE("A multiple-producer conflict is reported even when an unrelated cycle also exists",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}}),
      pass("B", {RawUsage{0, UsageKind::Write}}),
      pass("C", {RawUsage{1, UsageKind::Write}, RawUsage{2, UsageKind::Read}}),
      pass("D", {RawUsage{2, UsageKind::Write}, RawUsage{1, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("conflicted"), resource("r1"), resource("r2")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<MultipleProducersError>(result.error()));
  REQUIRE_FALSE(std::holds_alternative<DependencyCycleError>(result.error()));
}

TEST_CASE("Independent passes with no usage relationship keep declaration order", "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {pass("first"), pass("second"), pass("third")};

  const auto result = compile(passes, {});

  REQUIRE(result.isOk());
  const CompiledGraphData& data = result.value();
  REQUIRE(data.passesInOrder.size() == 3);
  REQUIRE(data.passesInOrder[0].label == "first");
  REQUIRE(data.passesInOrder[1].label == "second");
  REQUIRE(data.passesInOrder[2].label == "third");
}

TEST_CASE("A producer whose resource is never read is retained", "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {pass("producer", {RawUsage{0, UsageKind::Write}})};
  const std::vector<RawResource> resources = {resource("r0")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isOk());
  REQUIRE(result.value().passesInOrder.size() == 1);
  REQUIRE(result.value().edges.empty());
}

TEST_CASE("A two-pass, two-resource cycle is detected with a deterministic witness",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}, RawUsage{1, UsageKind::Read}}),
      pass("B", {RawUsage{1, UsageKind::Write}, RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0"), resource("r1")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<DependencyCycleError>(result.error()));
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 2);
  REQUIRE(error.passes[0].declarationIndex == 0);
  REQUIRE(error.passes[1].declarationIndex == 1);
}

TEST_CASE("A longer, three-pass cycle is detected", "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}, RawUsage{2, UsageKind::Read}}),
      pass("B", {RawUsage{1, UsageKind::Write}, RawUsage{0, UsageKind::Read}}),
      pass("C", {RawUsage{2, UsageKind::Write}, RawUsage{1, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0"), resource("r1"), resource("r2")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<DependencyCycleError>(result.error()));
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 3);
  REQUIRE(error.passes[0].declarationIndex == 0);
  REQUIRE(error.passes[1].declarationIndex == 1);
  REQUIRE(error.passes[2].declarationIndex == 2);
}

TEST_CASE(
    "A downstream pass with a smaller declarationIndex than any cycle member is excluded from the witness",
    "[render_graph][compile_algorithm]") {
  // Pass 0 is declared first (smallest declarationIndex) but merely reads
  // pass 1's output -- it is left un-output by Kahn's algorithm (since
  // its producer, pass 1, is itself stuck in the 1<->2 cycle), but it is
  // not part of the cycle, and must not appear in the witness.
  const std::vector<RawPass> passes = {
      pass("downstream", {RawUsage{2, UsageKind::Read}}),
      pass("A", {RawUsage{0, UsageKind::Write}, RawUsage{1, UsageKind::Read}, RawUsage{2, UsageKind::Write}}),
      pass("B", {RawUsage{1, UsageKind::Write}, RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0"), resource("r1"), resource("r2")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  REQUIRE(std::holds_alternative<DependencyCycleError>(result.error()));
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 2);
  for (const PassDiagnostic& diagnostic : error.passes) {
    REQUIRE(diagnostic.declarationIndex != 0);
  }
}

TEST_CASE("When more than one independent cycle exists, the same one is reported deterministically",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}, RawUsage{1, UsageKind::Read}}),
      pass("B", {RawUsage{1, UsageKind::Write}, RawUsage{0, UsageKind::Read}}),
      pass("C", {RawUsage{2, UsageKind::Write}, RawUsage{3, UsageKind::Read}}),
      pass("D", {RawUsage{3, UsageKind::Write}, RawUsage{2, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0"), resource("r1"), resource("r2"), resource("r3")};

  const auto first = compile(passes, resources);
  const auto second = compile(passes, resources);

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

TEST_CASE("A cycle whose passes carry duplicate labels is still disambiguated by declarationIndex",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("dup", {RawUsage{0, UsageKind::Write}, RawUsage{1, UsageKind::Read}}),
      pass("dup", {RawUsage{1, UsageKind::Write}, RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0"), resource("r1")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 2);
  REQUIRE(error.passes[0].label == "dup");
  REQUIRE(error.passes[1].label == "dup");
  REQUIRE(error.passes[0].declarationIndex != error.passes[1].declarationIndex);
}

TEST_CASE("Every adjacent pair in a reported witness, including the wrap-around pair, is a real derived edge",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}, RawUsage{2, UsageKind::Read}}),
      pass("B", {RawUsage{1, UsageKind::Write}, RawUsage{0, UsageKind::Read}}),
      pass("C", {RawUsage{2, UsageKind::Write}, RawUsage{1, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0"), resource("r1"), resource("r2")};

  const auto result = compile(passes, resources);

  REQUIRE(result.isErr());
  const DependencyCycleError& error = std::get<DependencyCycleError>(result.error());
  REQUIRE(error.passes.size() == 3);

  // This fixture's only derived edges are 0->1, 1->2, 2->0 (each pass
  // reads the resource produced by the pass whose index is one lower,
  // wrapping around) -- the reported witness must trace exactly that
  // cycle, in order, including the wrap-around pair.
  auto isRealEdge = [](std::size_t from, std::size_t to) {
    return (from == 0 && to == 1) || (from == 1 && to == 2) || (from == 2 && to == 0);
  };
  for (std::size_t i = 0; i < error.passes.size(); ++i) {
    const std::size_t from = error.passes[i].declarationIndex;
    const std::size_t to = error.passes[(i + 1) % error.passes.size()].declarationIndex;
    REQUIRE(isRealEdge(from, to));
  }
}

TEST_CASE("Repeated compilation of the same graph description yields an equivalent success result",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("producer", {RawUsage{0, UsageKind::Write}}),
      pass("reader", {RawUsage{0, UsageKind::Read}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto first = compile(passes, resources);
  const auto second = compile(passes, resources);

  REQUIRE(first.isOk());
  REQUIRE(second.isOk());
  REQUIRE(first.value().passesInOrder.size() == second.value().passesInOrder.size());
  for (std::size_t i = 0; i < first.value().passesInOrder.size(); ++i) {
    REQUIRE(first.value().passesInOrder[i].label == second.value().passesInOrder[i].label);
  }
  REQUIRE(first.value().edges.size() == second.value().edges.size());
  for (std::size_t i = 0; i < first.value().edges.size(); ++i) {
    REQUIRE(first.value().edges[i].from == second.value().edges[i].from);
    REQUIRE(first.value().edges[i].to == second.value().edges[i].to);
  }
}

TEST_CASE("Repeated compilation of the same erroring graph description yields an equivalent error",
          "[render_graph][compile_algorithm]") {
  const std::vector<RawPass> passes = {
      pass("A", {RawUsage{0, UsageKind::Write}}),
      pass("B", {RawUsage{0, UsageKind::Write}}),
  };
  const std::vector<RawResource> resources = {resource("r0")};

  const auto first = compile(passes, resources);
  const auto second = compile(passes, resources);

  REQUIRE(first.isErr());
  REQUIRE(second.isErr());
  const MultipleProducersError& firstError = std::get<MultipleProducersError>(first.error());
  const MultipleProducersError& secondError = std::get<MultipleProducersError>(second.error());
  REQUIRE(firstError.resource.declarationIndex == secondError.resource.declarationIndex);
  REQUIRE(firstError.resource.label == secondError.resource.label);
  REQUIRE(firstError.producers.size() == secondError.producers.size());
  for (std::size_t i = 0; i < firstError.producers.size(); ++i) {
    REQUIRE(firstError.producers[i].declarationIndex == secondError.producers[i].declarationIndex);
  }
}

}  // namespace

#include "compile_algorithm.h"

#include <atlantis/render_graph/compile_error.h>
#include <atlantis/result.h>

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace atlantis::render_graph::detail {

namespace {

constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

using DeclEdge = std::pair<std::size_t, std::size_t>;  // (producer declIndex, reader declIndex)

// Step 1: verify at most one producer per resource, iterating resources
// in ascending declarationIndex order and stopping at the first
// violation. Also returns, for every resource, its single producer's
// declaration index (or kInvalidIndex if it has none) -- computed once
// here so step 2 never re-scans usages to find it again.
[[nodiscard]] atlantis::Result<std::vector<std::size_t>, CompileError> findProducers(
    const std::vector<RawPass>& passes, const std::vector<RawResource>& resources) {
  std::vector<std::size_t> producerOfResource(resources.size(), kInvalidIndex);

  for (std::size_t r = 0; r < resources.size(); ++r) {
    std::vector<std::size_t> producers;  // distinct pass indices, ascending
    for (std::size_t p = 0; p < passes.size(); ++p) {
      bool writesR = false;
      for (const RawUsage& usage : passes[p].usages) {
        if (usage.resourceIndex == r && usage.kind == UsageKind::Write) {
          writesR = true;
          break;
        }
      }
      if (writesR) producers.push_back(p);
    }

    if (producers.size() > 1) {
      std::vector<PassDiagnostic> producerDiagnostics;
      producerDiagnostics.reserve(producers.size());
      for (std::size_t p : producers) producerDiagnostics.push_back(PassDiagnostic{p, passes[p].label});
      return atlantis::Result<std::vector<std::size_t>, CompileError>::Err(
          MultipleProducersError{ResourceDiagnostic{r, resources[r].label}, std::move(producerDiagnostics)});
    }

    if (producers.size() == 1) producerOfResource[r] = producers[0];
  }

  return atlantis::Result<std::vector<std::size_t>, CompileError>::Ok(std::move(producerOfResource));
}

// Steps 2-3: derive producer -> reader edges from Read usages against
// each resource's single producer (step 1's result), then de-duplicate
// via an ordered std::set -- never std::unordered_set/unordered_map --
// so the resulting edge sequence is already deterministic, ascending by
// (from, to).
[[nodiscard]] std::vector<DeclEdge> deriveEdges(const std::vector<RawPass>& passes,
                                                 const std::vector<std::size_t>& producerOfResource) {
  std::set<DeclEdge> dedupedEdges;

  for (std::size_t p = 0; p < passes.size(); ++p) {
    for (const RawUsage& usage : passes[p].usages) {
      if (usage.kind != UsageKind::Read) continue;
      const std::size_t producer = producerOfResource[usage.resourceIndex];
      if (producer == kInvalidIndex) continue;  // producer-less resource: no edge
      if (producer == p) continue;              // structurally impossible (Builder rejects
                                                 // same-pass read+write); documents the invariant
      dedupedEdges.insert(DeclEdge{producer, p});
    }
  }

  return std::vector<DeclEdge>(dedupedEdges.begin(), dedupedEdges.end());
}

// Step 4: Kahn's algorithm with a declaration-order tie-break --
// deliberately O(passes^2) linear "scan ascending from zero" instead of
// a priority queue (Plan 0005 Section 6's "Complexity" subsection).
// Returns the compiled order (declaration indices); its size is less
// than passes.size() exactly when a cycle prevented full output.
[[nodiscard]] std::vector<std::size_t> topologicalSort(std::size_t passCount, const std::vector<DeclEdge>& edges) {
  std::vector<std::size_t> inDegree(passCount, 0);
  for (const DeclEdge& edge : edges) ++inDegree[edge.second];

  std::vector<bool> output(passCount, false);
  std::vector<std::size_t> compiledOrder;
  compiledOrder.reserve(passCount);

  for (std::size_t iteration = 0; iteration < passCount; ++iteration) {
    std::size_t next = kInvalidIndex;
    for (std::size_t p = 0; p < passCount; ++p) {
      if (!output[p] && inDegree[p] == 0) {
        next = p;
        break;
      }
    }
    if (next == kInvalidIndex) break;  // no ready pass left: a cycle remains (step 6)

    output[next] = true;
    compiledOrder.push_back(next);
    for (const DeclEdge& edge : edges) {
      if (edge.first == next) --inDegree[edge.second];
    }
  }

  return compiledOrder;
}

// Step 6: three-color DFS over exactly the remaining (not-output)
// subgraph, using an explicit stack (never recursion), to extract one
// concrete cycle -- not "every not-output pass," since a pass can be
// merely downstream of a cycle without being part of it.
[[nodiscard]] std::vector<std::size_t> findCycleWitness(std::size_t passCount, const std::vector<DeclEdge>& edges,
                                                         const std::vector<bool>& output) {
  std::vector<bool> remaining(passCount);
  for (std::size_t p = 0; p < passCount; ++p) remaining[p] = !output[p];

  // Adjacency restricted to the remaining subgraph, ascending by target
  // -- `edges` is already sorted ascending by (from, to) (step 3), so
  // grouping by `from` preserves ascending target order with no
  // additional sort.
  std::vector<std::vector<std::size_t>> adjacency(passCount);
  for (const DeclEdge& edge : edges) {
    if (remaining[edge.first] && remaining[edge.second]) adjacency[edge.first].push_back(edge.second);
  }

  enum class Color { Unvisited, Visiting, Finished };
  std::vector<Color> color(passCount, Color::Unvisited);

  for (std::size_t root = 0; root < passCount; ++root) {
    if (!remaining[root] || color[root] != Color::Unvisited) continue;

    // Explicit path stack: {passIndex, nextAdjacencyCursor}.
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    stack.push_back({root, 0});
    color[root] = Color::Visiting;

    while (!stack.empty()) {
      const std::size_t topIndex = stack.size() - 1;
      const std::size_t p = stack[topIndex].first;
      const std::size_t cursor = stack[topIndex].second;

      if (cursor < adjacency[p].size()) {
        const std::size_t q = adjacency[p][cursor];
        stack[topIndex].second = cursor + 1;  // advance before any possible reallocation below

        if (color[q] == Color::Unvisited) {
          color[q] = Color::Visiting;
          stack.push_back({q, 0});
        } else if (color[q] == Color::Visiting) {
          // Back edge found: the cycle is the path-stack suffix from q
          // to the current top, not the whole remaining subgraph.
          std::vector<std::size_t> cycle;
          bool collecting = false;
          for (const auto& frame : stack) {
            if (frame.first == q) collecting = true;
            if (collecting) cycle.push_back(frame.first);
          }

          // Canonical rotation: begin at the cycle's own smallest
          // declarationIndex member, preserving edge-following direction.
          const auto minIt = std::min_element(cycle.begin(), cycle.end());
          const std::size_t minPos = static_cast<std::size_t>(minIt - cycle.begin());
          std::vector<std::size_t> rotated;
          rotated.reserve(cycle.size());
          for (std::size_t i = 0; i < cycle.size(); ++i) rotated.push_back(cycle[(minPos + i) % cycle.size()]);
          return rotated;
        }
        // color[q] == Finished: already fully explored, skip.
      } else {
        color[p] = Color::Finished;
        stack.pop_back();
      }
    }
  }

  return {};  // unreachable in practice: topologicalSort() only calls this when a cycle exists
}

}  // namespace

atlantis::Result<CompiledGraphData, CompileError> compile(const std::vector<RawPass>& passes,
                                                            const std::vector<RawResource>& resources) {
  // Step 1.
  auto producersResult = findProducers(passes, resources);
  if (producersResult.isErr()) {
    return atlantis::Result<CompiledGraphData, CompileError>::Err(std::move(producersResult.error()));
  }
  const std::vector<std::size_t>& producerOfResource = producersResult.value();

  // Steps 2-3.
  const std::vector<DeclEdge> declEdges = deriveEdges(passes, producerOfResource);

  // Step 4.
  const std::vector<std::size_t> compiledOrder = topologicalSort(passes.size(), declEdges);

  // Step 5: success.
  if (compiledOrder.size() == passes.size()) {
    std::vector<std::size_t> declarationToCompiledPosition(passes.size(), kInvalidIndex);
    for (std::size_t position = 0; position < compiledOrder.size(); ++position) {
      declarationToCompiledPosition[compiledOrder[position]] = position;
    }

    CompiledGraphData data;
    data.passesInOrder.reserve(compiledOrder.size());
    for (std::size_t declIndex : compiledOrder)
      data.passesInOrder.push_back(CompiledPassData{passes[declIndex].label, declIndex});

    data.edges.reserve(declEdges.size());
    for (const DeclEdge& edge : declEdges) {
      data.edges.push_back(
          CompiledEdge{declarationToCompiledPosition[edge.first], declarationToCompiledPosition[edge.second]});
    }

    return atlantis::Result<CompiledGraphData, CompileError>::Ok(std::move(data));
  }

  // Step 6: failure -- a cycle prevented full output.
  std::vector<bool> output(passes.size(), false);
  for (std::size_t declIndex : compiledOrder) output[declIndex] = true;

  const std::vector<std::size_t> witnessIndices = findCycleWitness(passes.size(), declEdges, output);

  std::vector<PassDiagnostic> witness;
  witness.reserve(witnessIndices.size());
  for (std::size_t declIndex : witnessIndices) witness.push_back(PassDiagnostic{declIndex, passes[declIndex].label});

  return atlantis::Result<CompiledGraphData, CompileError>::Err(DependencyCycleError{std::move(witness)});
}

}  // namespace atlantis::render_graph::detail

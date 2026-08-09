#include <atlantis/render_graph/compiled_graph.h>

#include <atlantis/assert.h>

#include <utility>

namespace atlantis::render_graph {

CompiledGraph::CompiledGraph(std::vector<PassRecord> passesInOrder, std::vector<CompiledDependencyEdge> edges)
    : passesInOrder_(std::move(passesInOrder)), edges_(std::move(edges)) {}

std::size_t CompiledGraph::passCount() const noexcept { return passesInOrder_.size(); }

CompiledPassId CompiledGraph::passOrder(std::size_t position) const {
  const bool valid = position < passesInOrder_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return CompiledPassId{};  // invalid sentinel, never a real position
  return CompiledPassId(position);
}

std::string_view CompiledGraph::label(CompiledPassId pass) const {
  const bool valid = pass.index() < passesInOrder_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return {};  // see this header's callout on why this is an
                          // acceptable, if not perfectly distinguishable,
                          // fallback for a diagnostic-only accessor
  return passesInOrder_[pass.index()].label;
}

std::size_t CompiledGraph::dependencyCount() const noexcept { return edges_.size(); }

CompiledDependencyEdge CompiledGraph::dependency(std::size_t i) const {
  const bool valid = i < edges_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return CompiledDependencyEdge{};  // both endpoints invalid-sentinel
  return edges_[i];
}

}  // namespace atlantis::render_graph

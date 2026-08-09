#include <atlantis/render_graph/compiled_graph.h>

#include <atlantis/assert.h>

#include <utility>

namespace atlantis::render_graph {

CompiledGraph::CompiledGraph(std::vector<PassRecord> passesInOrder, std::vector<EdgeRecord> edges,
                              std::vector<ResourceRecord> resources)
    : passesInOrder_(std::move(passesInOrder)), resources_(std::move(resources)) {
  edges_.reserve(edges.size());
  for (const EdgeRecord& edge : edges) {
    edges_.push_back(CompiledDependencyEdge{CompiledPassId(edge.from), CompiledPassId(edge.to)});
  }
}

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

std::size_t CompiledGraph::resourceCount() const noexcept { return resources_.size(); }

CompiledResourceId CompiledGraph::resourceAt(std::size_t index) const {
  const bool valid = index < resources_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return CompiledResourceId{};
  return CompiledResourceId(index);
}

std::string_view CompiledGraph::label(CompiledResourceId resource) const {
  const bool valid = resource.index() < resources_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return {};
  return resources_[resource.index()].label;
}

bool CompiledGraph::hasProducer(CompiledResourceId resource) const noexcept {
  const bool valid = resource.index() < resources_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return false;
  return resources_[resource.index()].hasProducer;
}

bool CompiledGraph::requiresRhiBinding(CompiledResourceId resource) const noexcept {
  const bool valid = resource.index() < resources_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return false;
  for (const PassRecord& pass : passesInOrder_) {
    for (const UsageRecord& usage : pass.usages) {
      if (usage.resourceIndex == resource.index() && usage.state.has_value()) return true;
    }
  }
  return false;
}

std::size_t CompiledGraph::usageCount(CompiledPassId pass) const noexcept {
  const bool valid = pass.index() < passesInOrder_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return 0;
  return passesInOrder_[pass.index()].usages.size();
}

CompiledResourceUsage CompiledGraph::usage(CompiledPassId pass, std::size_t index) const {
  const bool validPass = pass.index() < passesInOrder_.size();
  ATLANTIS_CHECK(validPass);
  if (!validPass) return CompiledResourceUsage{};
  const std::vector<UsageRecord>& usages = passesInOrder_[pass.index()].usages;
  const bool validIndex = index < usages.size();
  ATLANTIS_CHECK(validIndex);
  if (!validIndex) return CompiledResourceUsage{};
  const UsageRecord& record = usages[index];
  return CompiledResourceUsage{CompiledResourceId(record.resourceIndex), record.isWrite, record.state};
}

const PassExecuteFn& CompiledGraph::passExecuteFn(CompiledPassId pass) const {
  static const PassExecuteFn kEmpty{};
  const bool valid = pass.index() < passesInOrder_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return kEmpty;
  return passesInOrder_[pass.index()].executeFn;
}

}  // namespace atlantis::render_graph

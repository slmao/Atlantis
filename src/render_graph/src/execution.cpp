#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>

#include <unordered_map>

namespace atlantis::render_graph {

namespace {

using atlantis::rhi::ResourceState;

[[nodiscard]] const ResourceBinding* findBinding(const std::vector<ResourceBinding>& bindings,
                                                   CompiledResourceId resource) {
  for (const ResourceBinding& binding : bindings) {
    if (binding.resource == resource) return &binding;
  }
  return nullptr;
}

[[nodiscard]] bool hasReadUsage(const CompiledGraph& graph, CompiledResourceId resource) {
  for (std::size_t position = 0; position < graph.passCount(); ++position) {
    const CompiledPassId pass = graph.passOrder(position);
    for (std::size_t i = 0; i < graph.usageCount(pass); ++i) {
      const CompiledResourceUsage usage = graph.usage(pass, i);
      if (usage.resource == resource && !usage.isWrite) return true;
    }
  }
  return false;
}

}  // namespace

void execute(const CompiledGraph& graph, const std::vector<ResourceBinding>& bindings,
             atlantis::rhi::CommandList& commandList) {
  // Guard 1: every ResourceState-tagged usage's resource must have a
  // supplied binding. Checked once per resource, over the whole graph.
  for (std::size_t resourceIndex = 0; resourceIndex < graph.resourceCount(); ++resourceIndex) {
    const CompiledResourceId resource = graph.resourceAt(resourceIndex);
    if (!graph.requiresRhiBinding(resource)) continue;
    const bool bound = findBinding(bindings, resource) != nullptr;
    ATLANTIS_CHECK_MSG(bound, "execute(): a ResourceState-tagged usage has no supplied binding");
  }

  // Guard 2: no bound resource may have any declared read usage anywhere
  // in the graph -- protects ADR-0019's always-Undefined-incoming-layout
  // premise structurally. A bound resource may still have a write usage
  // (its producer) -- only a read usage is forbidden; see this header's
  // own comment and the Plan's Human Review Confirmations Received.
  for (const ResourceBinding& binding : bindings) {
    const bool noReadUsage = !hasReadUsage(graph, binding.resource);
    ATLANTIS_CHECK_MSG(noReadUsage, "execute(): a bound RenderTarget has a declared read usage");
  }

  std::unordered_map<std::size_t, ResourceState> currentState;

  for (std::size_t position = 0; position < graph.passCount(); ++position) {
    const CompiledPassId pass = graph.passOrder(position);

    for (std::size_t i = 0; i < graph.usageCount(pass); ++i) {
      const CompiledResourceUsage usage = graph.usage(pass, i);
      if (!usage.state.has_value()) continue;  // untagged Spec 0005 usage -- no transition bookkeeping

      const ResourceBinding* binding = findBinding(bindings, usage.resource);
      // Guard 1's ATLANTIS_CHECK_MSG above already reported a missing
      // binding for this resource; under a non-terminating handler
      // (test), skip this usage's transition rather than dereferencing a
      // missing binding -- the same UB-safe check-then-continue pattern
      // RenderGraphBuilder already uses.
      if (binding == nullptr) continue;

      const std::size_t key = usage.resource.index();
      const ResourceState previous = currentState.count(key) ? currentState[key] : ResourceState::Undefined;
      if (previous != *usage.state) {
        commandList.transitionResource(*binding->target, previous, *usage.state);
        currentState[key] = *usage.state;
      }
    }

    // A declared-but-never-.setExecute()'d pass is legal (Spec 0005 pass
    // retention) -- an empty PassExecuteFn is simply not invoked.
    const PassExecuteFn& executeFn = graph.passExecuteFn(pass);
    if (executeFn) {
      executeFn(commandList);
    }
  }

  // Trailing PresentSource transition: only for a bound resource actually
  // touched by at least one usage (currentState has an entry) -- no
  // spurious transition for an unused binding.
  for (const ResourceBinding& binding : bindings) {
    const std::size_t key = binding.resource.index();
    const auto it = currentState.find(key);
    if (it == currentState.end()) continue;
    if (it->second != ResourceState::PresentSource) {
      commandList.transitionResource(*binding.target, it->second, ResourceState::PresentSource);
      it->second = ResourceState::PresentSource;
    }
  }
}

}  // namespace atlantis::render_graph

#pragma once

#include <vector>

#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>

namespace atlantis::render_graph {

// Binds one compiled-local resource to a live RHI RenderTarget for
// exactly one execute() call -- frame-scoped, not persisted on the
// builder or CompiledGraph (ADR-0021). target must outlive the
// execute() call; execute() does not take ownership of it.
struct ResourceBinding {
  CompiledResourceId resource;
  atlantis::rhi::RenderTarget* target = nullptr;
};

// Walks graph's compiled pass order once. For each pass, for each
// ResourceState-tagged usage whose declared state differs from that
// resource's most-recently-recorded state, records a transitionResource()
// call before invoking the pass's execution callback. Inserts one
// trailing transitionResource() to ResourceState::PresentSource for
// every bound resource that was actually touched by at least one usage.
// Records only -- never calls Device::submit() or Presentation::present()
// (ADR-0021). Two preconditions are guaranteed-detectable programmer
// errors (ATLANTIS_CHECK_MSG), not Result-typed: every ResourceState-
// tagged usage in graph must have a matching entry in bindings; no bound
// resource may have any declared read usage anywhere in graph (protects
// ADR-0019's always-Undefined-incoming-layout premise structurally,
// regardless of whether that resource also has a write producer --
// "producer-less" in ADR-0021 describes the bound RenderTarget's
// physical origin, not a ban on the logical resource having a producer;
// see plans/0006-rhi-render-graph-frame-execution-foundation.md's Human
// Review Confirmations Received). A binding that fails Guard 1 is
// skipped (UB-safe check-then-continue), not dereferenced, under a
// non-terminating failure handler. Spec 0005's plain untagged usages
// need no binding and produce no transition. Not thread-safe; single
// Phase 1 frame thread only (ADR-0004).
void execute(const CompiledGraph& graph, const std::vector<ResourceBinding>& bindings,
             atlantis::rhi::CommandList& commandList);

}  // namespace atlantis::render_graph

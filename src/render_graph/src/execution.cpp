#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>

#include <unordered_map>
#include <unordered_set>

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

// Spec 0007/ADR-0026: a pass is a "draw pass" iff any of its declared
// usages carries ColorAttachmentOutput or DepthAttachmentReadWrite -- and
// only these two states, never ColorAttachmentWrite (Spec 0006's existing
// clearColor()-only pass stays structurally un-wrapped by this rule).
[[nodiscard]] bool isDrawPass(const CompiledGraph& graph, CompiledPassId pass) {
  for (std::size_t i = 0; i < graph.usageCount(pass); ++i) {
    const CompiledResourceUsage usage = graph.usage(pass, i);
    if (!usage.state.has_value()) continue;
    if (*usage.state == ResourceState::ColorAttachmentOutput || *usage.state == ResourceState::DepthAttachmentReadWrite) {
      return true;
    }
  }
  return false;
}

}  // namespace

void execute(const CompiledGraph& graph, const std::vector<ResourceBinding>& bindings,
             atlantis::rhi::CommandList& commandList) {
  // Guard 0 (Spec 0007; widened to three kinds by Spec 0016, four by
  // Plan 0024 Milestone 2): every entry binds exactly one of
  // target/depthTexture/sampledTexture/hdrColorTarget, and no two
  // entries bind the same resource. This is the real mechanism that
  // rejects both zero-resource and multi-resource ambiguity uniformly
  // for all four kinds.
  std::unordered_set<std::size_t> seenBindingResources;
  for (const ResourceBinding& binding : bindings) {
    const int boundCount = (binding.target != nullptr) + (binding.depthTexture != nullptr) +
                            (binding.sampledTexture != nullptr) + (binding.hdrColorTarget != nullptr);
    ATLANTIS_CHECK_MSG(
        boundCount == 1,
        "ResourceBinding must bind exactly one of target/depthTexture/sampledTexture/hdrColorTarget");
    const auto [it, inserted] = seenBindingResources.insert(binding.resource.index());
    (void)it;
    ATLANTIS_CHECK_MSG(inserted, "ResourceBinding must not bind the same resource twice");
  }

  // Guard 1: every ResourceState-tagged usage's resource must have a
  // supplied binding -- generalized to depthTexture entries too (findBinding
  // matches by resource id regardless of which field is set).
  for (std::size_t resourceIndex = 0; resourceIndex < graph.resourceCount(); ++resourceIndex) {
    const CompiledResourceId resource = graph.resourceAt(resourceIndex);
    if (!graph.requiresRhiBinding(resource)) continue;
    const bool bound = findBinding(bindings, resource) != nullptr;
    ATLANTIS_CHECK_MSG(bound, "execute(): a ResourceState-tagged usage has no supplied binding");
  }

  // Guard 2 (scope unchanged -- target entries only): no bound RenderTarget
  // may have any declared read usage anywhere in the graph. Not checked for
  // depthTexture entries (ADR-0026 -- depth-test read is legitimate).
  for (const ResourceBinding& binding : bindings) {
    if (binding.target == nullptr) continue;
    const bool noReadUsage = !hasReadUsage(graph, binding.resource);
    ATLANTIS_CHECK_MSG(noReadUsage, "execute(): a bound RenderTarget has a declared read usage");
  }

  // Every bound resource (color and depth) starts each execute() call from
  // Undefined -- this map is entirely local to this call.
  std::unordered_map<std::size_t, ResourceState> currentState;

  for (std::size_t position = 0; position < graph.passCount(); ++position) {
    const CompiledPassId pass = graph.passOrder(position);
    const bool drawPass = isDrawPass(graph, pass);

    const ResourceBinding* colorBinding = nullptr;
    const ResourceBinding* depthBinding = nullptr;
    bool depthUsagePresent = false;

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

      if (drawPass && *usage.state == ResourceState::ColorAttachmentOutput) {
        colorBinding = binding;
      }
      if (drawPass && *usage.state == ResourceState::DepthAttachmentReadWrite) {
        depthUsagePresent = true;
        depthBinding = binding;
      }

      atlantis::rhi::RenderTarget* targetPtr = binding->target;
      atlantis::rhi::Texture* depthPtr = binding->depthTexture;
      atlantis::rhi::SampledTexture* sampledTexturePtr = binding->sampledTexture;
      atlantis::rhi::HdrColorTarget* hdrColorTargetPtr = binding->hdrColorTarget;

      const std::size_t key = usage.resource.index();
      const ResourceState previous = currentState.count(key) ? currentState[key] : binding->incomingState;
      if (previous != *usage.state) {
        if (targetPtr != nullptr) {
          commandList.transitionResource(*targetPtr, previous, *usage.state);
        } else if (depthPtr != nullptr) {
          commandList.transitionResource(*depthPtr, previous, *usage.state);
        } else if (sampledTexturePtr != nullptr) {
          commandList.transitionResource(*sampledTexturePtr, previous, *usage.state);
        } else {
          commandList.transitionResource(*hdrColorTargetPtr, previous, *usage.state);
        }
        currentState[key] = *usage.state;
      }
    }

    // A declared-but-never-.setExecute()'d pass is legal (Spec 0005 pass
    // retention) -- an empty PassExecuteFn is simply not invoked.
    const PassExecuteFn& executeFn = graph.passExecuteFn(pass);

    if (drawPass) {
      // UB-safe check-then-skip, mirroring the binding-not-found handling
      // above: Guard 1 already reported a missing binding, but under a
      // non-terminating handler (test) execution continues past that
      // check -- colorBinding/depthBinding must never be dereferenced
      // here without first confirming they were actually found.
      const bool canBeginRendering = colorBinding != nullptr && (!depthUsagePresent || depthBinding != nullptr);
      if (canBeginRendering) {
        // Plan 0024 Milestone 2: the pass writing ColorAttachmentOutput
        // may be bound via either the target field (existing draw pass,
        // a real RenderTarget) or the hdrColorTarget field (the new
        // geometry pass writing into the HDR intermediate) -- Guard 0
        // already guarantees colorBinding has exactly one of the two
        // set, so exactly one of these two branches ever fires.
        atlantis::rhi::Texture* depthPtr = depthUsagePresent ? depthBinding->depthTexture : nullptr;
        const float depthClearValue = depthUsagePresent ? depthBinding->depthClear : 1.0f;
        if (colorBinding->target != nullptr) {
          commandList.beginRendering(*colorBinding->target, depthPtr, colorBinding->colorClear, depthClearValue);
        } else {
          commandList.beginRendering(*colorBinding->hdrColorTarget, depthPtr, colorBinding->colorClear,
                                      depthClearValue);
        }
        if (executeFn) {
          executeFn(commandList);
        }
        commandList.endRendering();
      }
      // else: skip beginRendering/executeFn/endRendering entirely for this
      // pass -- Guard 1's check already reported the missing binding;
      // nothing safe remains to record for it.
    } else {
      if (executeFn) {
        executeFn(commandList);
      }
    }
  }

  // Trailing transition to the bound entry's finalState (Spec
  // 0010/ADR-0039; widened to sampledTexture bindings by Spec 0016/D4 --
  // the same mechanism, not a new one): only for a bound RenderTarget or
  // SampledTexture actually touched by at least one usage (currentState
  // has an entry) and whose finalState is not std::nullopt -- no spurious
  // transition for an unused binding or a binding that declines a
  // trailing transition, and never for a depthTexture entry (never
  // presented, never read back this round). For a sampledTexture upload
  // pass, this is what actually produces the TransferDestination ->
  // ShaderRead barrier *after* the pass's own copyBufferToTexture() call
  // -- execute()'s per-usage loop above records every transition for a
  // pass's declared usages before that pass's executeFn ever runs, so a
  // usage tagged ShaderRead directly on the upload pass itself would
  // wrongly land the barrier before the copy, not after it; the upload
  // pass therefore declares only the TransferDestination usage, and
  // ShaderRead is reached here, once, after every pass has executed.
  for (const ResourceBinding& binding : bindings) {
    if (binding.target == nullptr && binding.sampledTexture == nullptr && binding.hdrColorTarget == nullptr) {
      continue;
    }
    if (!binding.finalState.has_value()) continue;
    const std::size_t key = binding.resource.index();
    const auto it = currentState.find(key);
    if (it == currentState.end()) continue;
    if (it->second != *binding.finalState) {
      if (binding.target != nullptr) {
        commandList.transitionResource(*binding.target, it->second, *binding.finalState);
      } else if (binding.sampledTexture != nullptr) {
        commandList.transitionResource(*binding.sampledTexture, it->second, *binding.finalState);
      } else {
        commandList.transitionResource(*binding.hdrColorTarget, it->second, *binding.finalState);
      }
      it->second = *binding.finalState;
    }
  }
}

}  // namespace atlantis::render_graph

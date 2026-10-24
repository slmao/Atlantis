#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>
#include <atlantis/profile.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string_view>
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


// Spec 0059 / ADR-0115 D4: a pass's region is named after its declaration label;
// an empty label falls back to "pass_<position>". Both names are built in a
// caller-owned stack buffer, so a frame allocates nothing for them.
#if defined(ATLANTIS_PROFILING) || defined(ATLANTIS_GPU_MARKERS)
[[nodiscard]] std::string_view passRegionName(std::string_view label, std::size_t position, std::span<char> buffer) {
  if (!label.empty()) return label;
  const int written = std::snprintf(buffer.data(), buffer.size(), "pass_%zu", position);
  return std::string_view(buffer.data(), written > 0 ? static_cast<std::size_t>(written) : 0);
}
#endif
#if defined(ATLANTIS_PROFILING)
// "RecordPass:<label>", the CPU zone of a pass's recording.
[[nodiscard]] std::string_view recordPassZoneName(std::string_view region, std::span<char> buffer) {
  const int written = std::snprintf(buffer.data(), buffer.size(), "RecordPass:%.*s", static_cast<int>(region.size()), region.data());
  return std::string_view(buffer.data(), written > 0 ? std::min(static_cast<std::size_t>(written), buffer.size() - 1) : 0);
}
#endif

}  // namespace

std::vector<ResourceState> execute(const CompiledGraph& graph, const std::vector<ResourceBinding>& bindings,
                                   atlantis::rhi::CommandList& commandList) {
  // Guard 0 (Spec 0007; widened to three kinds by Spec 0016, four by
  // Plan 0024 Milestone 2, five by Plan 0027 Milestone 3): every entry
  // binds exactly one of target/depthTexture/sampledTexture/
  // hdrColorTarget/shadowMap, and no two entries bind the same resource.
  // This is the real mechanism that rejects both zero-resource and
  // multi-resource ambiguity uniformly for all five kinds.
  std::unordered_set<std::size_t> seenBindingResources;
  for (const ResourceBinding& binding : bindings) {
    const int boundCount = (binding.target != nullptr) + (binding.depthTexture != nullptr) +
                            (binding.sampledTexture != nullptr) + (binding.hdrColorTarget != nullptr) +
                            (binding.shadowMap != nullptr);
    ATLANTIS_CHECK_MSG(
        boundCount == 1,
        "ResourceBinding must bind exactly one of target/depthTexture/sampledTexture/hdrColorTarget/shadowMap");
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

  // Guard 3 (Plan 0066 P3, ADR-0133 D3): only a target or depthTexture entry
  // loads, and only from a state an earlier execute() left it in.
  for (const ResourceBinding& binding : bindings) {
    if (binding.load != atlantis::rhi::AttachmentLoad::Load) continue;
    ATLANTIS_CHECK_MSG(binding.target != nullptr || binding.depthTexture != nullptr,
                       "execute(): only a target or depthTexture binding can load its attachment");
    ATLANTIS_CHECK_MSG(binding.incomingState != ResourceState::Undefined,
                       "execute(): a Load binding enters Undefined -- there is nothing to load");
  }

  // Every bound resource starts each execute() call from its binding's
  // incomingState (Undefined by default) -- this map is entirely local to
  // this call, and holds every resource some pass used.
  std::unordered_map<std::size_t, ResourceState> currentState;

  for (std::size_t position = 0; position < graph.passCount(); ++position) {
    const CompiledPassId pass = graph.passOrder(position);
    const bool drawPass = isDrawPass(graph, pass);
#if defined(ATLANTIS_PROFILING) || defined(ATLANTIS_GPU_MARKERS)
    // The region opens before the pass's transitions and closes with this
    // iteration, after endRendering() or the callback (ADR-0115 D4).
    std::array<char, 64> regionBuffer{};
    const std::string_view regionName = passRegionName(graph.label(pass), position, regionBuffer);
    ATL_PROFILE_GPU_SCOPE(commandList, regionName);
#endif

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
      atlantis::rhi::ShadowMap* shadowMapPtr = binding->shadowMap;

      const std::size_t key = usage.resource.index();
      const ResourceState previous = currentState.count(key) ? currentState[key] : binding->incomingState;
      // Plan 0066 P3 (ADR-0133 D3): a draw pass loading an attachment it
      // already holds in this state still needs the earlier writes made
      // visible -- the same-state transition, a memory dependency only.
      const bool loadsAttachment = drawPass && binding->load == atlantis::rhi::AttachmentLoad::Load &&
                                   (*usage.state == ResourceState::ColorAttachmentOutput ||
                                    *usage.state == ResourceState::DepthAttachmentReadWrite);
      if (previous == *usage.state && loadsAttachment) {
        if (targetPtr != nullptr) {
          commandList.transitionResource(*targetPtr, previous, *usage.state);
        } else if (depthPtr != nullptr) {
          commandList.transitionResource(*depthPtr, previous, *usage.state);
        }
      }
      if (previous != *usage.state) {
        if (targetPtr != nullptr) {
          commandList.transitionResource(*targetPtr, previous, *usage.state);
        } else if (depthPtr != nullptr) {
          commandList.transitionResource(*depthPtr, previous, *usage.state);
        } else if (sampledTexturePtr != nullptr) {
          commandList.transitionResource(*sampledTexturePtr, previous, *usage.state);
        } else if (hdrColorTargetPtr != nullptr) {
          commandList.transitionResource(*hdrColorTargetPtr, previous, *usage.state);
        } else {
          // Plan 0027 Milestone 3 (ADR-0072 D-4): Guard 0 above already
          // guarantees exactly one of the five kinds is set -- shadowMapPtr
          // is the only remaining possibility once the first four have
          // been ruled out.
          commandList.transitionResource(*shadowMapPtr, previous, *usage.state);
        }
      }
      // Recorded whether or not a transition was: the resource was used.
      currentState[key] = *usage.state;
    }

    // A declared-but-never-.setExecute()'d pass is legal (Spec 0005 pass
    // retention) -- an empty PassExecuteFn is simply not invoked.
    const PassExecuteFn& executeFn = graph.passExecuteFn(pass);
#if defined(ATLANTIS_PROFILING)
    std::array<char, 80> zoneBuffer{};
    ATL_PROFILE_SCOPE_DYNAMIC(recordPassZoneName(regionName, zoneBuffer));
#endif

    if (drawPass) {
      // Plan 0027 Milestone 3 (ADR-0072 D-4): a pass whose only
      // DepthAttachmentReadWrite usage is bound via shadowMap (not
      // depthTexture) is genuinely depth-only -- it has no
      // ColorAttachmentOutput usage at all (the shadow-casting Pipeline's
      // own hasColorAttachment == false shape), so colorBinding is
      // always nullptr for it. This is the real gap the prior
      // canBeginRendering expression could not express: it required
      // colorBinding != nullptr unconditionally, silently skipping any
      // pass with no color attachment whatsoever.
      const bool depthOnly = depthUsagePresent && depthBinding != nullptr && depthBinding->shadowMap != nullptr;
      // UB-safe check-then-skip, mirroring the binding-not-found handling
      // above: Guard 1 already reported a missing binding, but under a
      // non-terminating handler (test) execution continues past that
      // check -- colorBinding/depthBinding must never be dereferenced
      // here without first confirming they were actually found.
      const bool canBeginRendering =
          depthOnly ? true : (colorBinding != nullptr && (!depthUsagePresent || depthBinding != nullptr));
      if (canBeginRendering) {
        if (depthOnly) {
          commandList.beginRendering(*depthBinding->shadowMap, depthBinding->depthClear);
        } else {
          // Plan 0024 Milestone 2: the pass writing ColorAttachmentOutput
          // may be bound via either the target field (existing draw pass,
          // a real RenderTarget) or the hdrColorTarget field (the new
          // geometry pass writing into the HDR intermediate) -- Guard 0
          // already guarantees colorBinding has exactly one of the two
          // set, so exactly one of these two branches ever fires.
          atlantis::rhi::Texture* depthPtr = depthUsagePresent ? depthBinding->depthTexture : nullptr;
          const float depthClearValue = depthUsagePresent ? depthBinding->depthClear : 1.0f;
          // Plan 0066 P3 (ADR-0133 D3): a Load binding begins loaded; a
          // scope with no Load is recorded exactly as before.
          const atlantis::rhi::AttachmentLoad colorLoad = colorBinding->load;
          const atlantis::rhi::AttachmentLoad depthLoad =
              depthUsagePresent ? depthBinding->load : atlantis::rhi::AttachmentLoad::Clear;
          const bool anyLoad =
              colorLoad == atlantis::rhi::AttachmentLoad::Load || depthLoad == atlantis::rhi::AttachmentLoad::Load;
          if (colorBinding->target != nullptr && anyLoad) {
            commandList.beginRendering(*colorBinding->target, depthPtr, colorBinding->colorClear, depthClearValue,
                                        colorLoad, depthLoad);
          } else if (colorBinding->target != nullptr) {
            commandList.beginRendering(*colorBinding->target, depthPtr, colorBinding->colorClear, depthClearValue);
          } else {
            ATLANTIS_CHECK_MSG(!anyLoad, "execute(): an HdrColorTarget scope cannot load its depth attachment");
            commandList.beginRendering(*colorBinding->hdrColorTarget, depthPtr, colorBinding->colorClear,
                                        depthClearValue);
          }
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

  // Plan 0066 P3 (ADR-0133 D3): where each bound resource was left.
  std::vector<ResourceState> endStates;
  endStates.reserve(bindings.size());
  for (const ResourceBinding& binding : bindings) {
    const auto it = currentState.find(binding.resource.index());
    endStates.push_back(it != currentState.end() ? it->second : binding.incomingState);
  }
  return endStates;
}

}  // namespace atlantis::render_graph

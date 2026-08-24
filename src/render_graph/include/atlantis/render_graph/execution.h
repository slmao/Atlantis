#pragma once

#include <optional>
#include <vector>

#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/texture.h>

namespace atlantis::render_graph {

// Binds one compiled-local resource to a live RHI RenderTarget, depth
// Texture, or SampledTexture for exactly one execute() call -- frame-
// scoped, not persisted on the builder or CompiledGraph (ADR-0021).
// Exactly one of target/depthTexture/sampledTexture must be non-null
// (Guard 0, below) -- a malformed binding with zero or more than one set
// is a programmer error, not a silently accepted ambiguous case.
// target/depthTexture/sampledTexture must outlive the execute() call;
// execute() does not take ownership of any of them. colorClear is used
// only when target != nullptr; depthClear only when depthTexture !=
// nullptr (Spec 0007/ADR-0026).
//
// sampledTexture (Spec 0016/ADR-0056): tracks ONLY the destination of a
// texture upload's own Undefined -> TransferDestination -> ShaderRead
// transition -- never used to track a texture already in ShaderRead
// being sampled by a later, unrelated draw pass; that consumption is a
// plain CommandList::bindTexture() call inside a pass callback, exactly
// like bindUniformBuffer() today, not a RenderGraph-tracked resource.
// The upload pass itself declares only a single TransferDestination
// usage (never a second, ShaderRead-tagged usage on the same pass) --
// execute() records every one of a pass's declared-usage transitions
// before invoking that pass's own executeFn, so a ShaderRead usage
// declared directly on the upload pass would land its barrier *before*
// the pass's own copyBufferToTexture() call, not after it. The trailing
// ShaderRead transition is reached the same way a target's own trailing
// finalState transition is: via this binding's own finalState field,
// below, applied once after every pass in the graph has executed.
//
// incomingState/finalState (Spec 0010/ADR-0039; finalState widened to
// sampledTexture bindings by Spec 0016/D4 -- the same mechanism, not a
// new one): meaningful only for a target- or sampledTexture-shaped entry
// (target != nullptr or sampledTexture != nullptr), ignored for a
// depthTexture entry. incomingState is the state the caller asserts the
// bound resource already holds when this execute() call begins seeding
// its own local tracking -- its default, ResourceState::Undefined, is
// correct only for a resource's first use within its CommandList; a
// caller reusing an already-transitioned target (e.g. the headless copy
// pass, which runs after Renderer::drawFrame() has already left its
// target in a known non-Undefined state) must supply the true incoming
// state explicitly. finalState, std::nullopt by default, is the state
// execute() leaves the bound resource in via one trailing
// transitionResource() call if its ending state differs -- std::nullopt
// means no trailing transition is inserted beyond whatever the last
// pass's own declared state already left it in. Neither field has a
// default a target- or sampledTexture-shaped call site may rely on being
// "correct" for its own purpose -- every such binding construction site
// in this codebase supplies an explicit finalState as a matter of caller
// discipline, not because the language forces it syntactically.
struct ResourceBinding {
  CompiledResourceId resource;
  atlantis::rhi::RenderTarget* target = nullptr;
  atlantis::rhi::ClearColorValue colorClear{};
  atlantis::rhi::Texture* depthTexture = nullptr;
  float depthClear = 1.0f;
  atlantis::rhi::SampledTexture* sampledTexture = nullptr;
  atlantis::rhi::ResourceState incomingState = atlantis::rhi::ResourceState::Undefined;
  std::optional<atlantis::rhi::ResourceState> finalState;
};

// Walks graph's compiled pass order once. For each pass, for each
// ResourceState-tagged usage whose declared state differs from that
// resource's most-recently-recorded state (seeded from the resource's
// bound incomingState, not always ResourceState::Undefined -- Spec
// 0010/ADR-0039), records a transitionResource() call before invoking the
// pass's execution callback. A pass is a "draw pass" (Spec 0007/ADR-0026)
// iff any of its usages carries ResourceState::ColorAttachmentOutput or
// ResourceState::DepthAttachmentReadWrite -- and only these two states,
// never ColorAttachmentWrite, which keeps Spec 0006's existing
// clearColor()-only pass structurally un-wrapped by this rule. A
// recognized draw pass's execution callback (if any) is invoked between a
// commandList.beginRendering()/endRendering() pair; every other pass's
// callback is invoked directly, exactly as Spec 0006 already does.
// Inserts one trailing transitionResource() to the bound entry's
// finalState (Spec 0010/ADR-0039; widened to sampledTexture bindings by
// Spec 0016/D4) for every bound RenderTarget or SampledTexture
// (target != nullptr or sampledTexture != nullptr) that was actually
// touched by at least one usage and whose finalState is not std::nullopt
// -- never for a depthTexture entry (never presented, never read back
// this round), and never when finalState is std::nullopt. Records only --
// never calls Device::submit() or Presentation::present() (ADR-0021).
//
// Guards, all guaranteed-detectable programmer errors
// (ATLANTIS_CHECK_MSG), not Result-typed:
// - Guard 0 (Spec 0007; widened to three kinds by Spec 0016): every
//   ResourceBinding entry has exactly one of target/depthTexture/
//   sampledTexture non-null, and bindings contains no two entries for
//   the same CompiledResourceId.
// - Guard 1 (unchanged in principle, scope generalized to depthTexture/
//   sampledTexture entries too): every ResourceState-tagged usage in
//   graph must have a matching entry in bindings.
// - Guard 2 (scope unchanged -- target entries only): no bound
//   RenderTarget may have any declared read usage anywhere in graph
//   (protects RenderTarget's own write-only contract -- see
//   rhi::RenderTarget's own doc comment -- independent of what
//   incomingState the caller supplies for it, Spec 0010/ADR-0039). Not
//   checked for depthTexture entries -- a depth-test read is legitimate,
//   expressed as part of the single DepthAttachmentReadWrite write usage
//   (ADR-0026).
//
// A binding that fails Guard 1 is skipped (UB-safe check-then-continue),
// not dereferenced, under a non-terminating failure handler -- likewise a
// draw pass whose color/depth binding was not found. Spec 0005's plain
// untagged usages need no binding and produce no transition. Every bound
// resource (color and depth) starts each execute() call from
// ResourceState::Undefined -- this function's own currentState bookkeeping
// is entirely local to one call, never persisted across calls. Not
// thread-safe; single Phase 1 frame thread only (ADR-0004).
void execute(const CompiledGraph& graph, const std::vector<ResourceBinding>& bindings,
             atlantis::rhi::CommandList& commandList);

}  // namespace atlantis::render_graph

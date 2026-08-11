#pragma once

#include <vector>

#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/texture.h>

namespace atlantis::render_graph {

// Binds one compiled-local resource to a live RHI RenderTarget or depth
// Texture for exactly one execute() call -- frame-scoped, not persisted on
// the builder or CompiledGraph (ADR-0021). Exactly one of target/
// depthTexture must be non-null (Guard 0, below) -- a malformed binding
// with both null or both non-null is a programmer error, not a silently
// accepted ambiguous case. target/depthTexture must outlive the execute()
// call; execute() does not take ownership of either. colorClear is used
// only when target != nullptr; depthClear only when depthTexture !=
// nullptr (Spec 0007/ADR-0026).
struct ResourceBinding {
  CompiledResourceId resource;
  atlantis::rhi::RenderTarget* target = nullptr;
  atlantis::rhi::ClearColorValue colorClear{};
  atlantis::rhi::Texture* depthTexture = nullptr;
  float depthClear = 1.0f;
};

// Walks graph's compiled pass order once. For each pass, for each
// ResourceState-tagged usage whose declared state differs from that
// resource's most-recently-recorded state, records a transitionResource()
// call before invoking the pass's execution callback. A pass is a "draw
// pass" (Spec 0007/ADR-0026) iff any of its usages carries
// ResourceState::ColorAttachmentOutput or
// ResourceState::DepthAttachmentReadWrite -- and only these two states,
// never ColorAttachmentWrite, which keeps Spec 0006's existing
// clearColor()-only pass structurally un-wrapped by this rule. A
// recognized draw pass's execution callback (if any) is invoked between a
// commandList.beginRendering()/endRendering() pair; every other pass's
// callback is invoked directly, exactly as Spec 0006 already does.
// Inserts one trailing transitionResource() to ResourceState::PresentSource
// for every bound RenderTarget (target != nullptr) that was actually
// touched by at least one usage -- never for a depthTexture entry (never
// presented, never read back this round). Records only -- never calls
// Device::submit() or Presentation::present() (ADR-0021).
//
// Guards, all guaranteed-detectable programmer errors
// (ATLANTIS_CHECK_MSG), not Result-typed:
// - Guard 0 (Spec 0007): every ResourceBinding entry has exactly one of
//   target/depthTexture non-null, and bindings contains no two entries
//   for the same CompiledResourceId.
// - Guard 1 (unchanged in principle, scope generalized to depthTexture
//   entries too): every ResourceState-tagged usage in graph must have a
//   matching entry in bindings.
// - Guard 2 (scope unchanged -- target entries only): no bound
//   RenderTarget may have any declared read usage anywhere in graph
//   (protects ADR-0019's always-Undefined-incoming-layout premise
//   structurally). Not checked for depthTexture entries -- a depth-test
//   read is legitimate, expressed as part of the single
//   DepthAttachmentReadWrite write usage (ADR-0026).
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

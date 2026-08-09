#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <atlantis/render_graph/compile_error.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/result.h>
#include <atlantis/rhi/types.h>

namespace atlantis::render_graph {

// Accumulates a graph-local description of passes, logical resources, and
// their read/write usage.
//
// Ownership: non-copyable, non-movable (see the deleted special members
// below), the sole, exclusive owner of its accumulated declarations
// (ADR-0017). Purely additive -- there is no operation to remove,
// replace, or edit an already-accumulated declaration (ADR-0017); a
// caller that needs a different graph description constructs a new
// builder. Every PassHandle/ResourceHandle it vends is scoped to this
// specific instance (this class's own stable `this` address is the
// provenance tag -- safe only because this class is non-movable) and
// remains valid for this instance's entire lifetime.
//
// Thread-safety: **not thread-safe.** Every method -- every declaration
// call and compile() itself -- must be called on the single Phase 1
// logical frame thread that owns this instance (ADR-0004); no method may
// be called concurrently with any other method on the same instance from
// a different thread.
class RenderGraphBuilder {
 public:
  RenderGraphBuilder() = default;
  ~RenderGraphBuilder() = default;

  RenderGraphBuilder(const RenderGraphBuilder&) = delete;
  RenderGraphBuilder& operator=(const RenderGraphBuilder&) = delete;
  RenderGraphBuilder(RenderGraphBuilder&&) = delete;
  RenderGraphBuilder& operator=(RenderGraphBuilder&&) = delete;

  // Creates a graph-local logical resource. `label` is an optional,
  // non-unique diagnostic label, copied into this builder's own owned
  // storage immediately -- never borrowed from the caller's string. A
  // resource that is never given a write usage by any pass is valid and
  // may still be read (Spec 0005 Functional Requirements, "Logical
  // resources").
  [[nodiscard]] ResourceHandle declareResource(std::string_view label = {});

  // Declares a pass. `label` is an optional, non-unique diagnostic label,
  // under the same rules as declareResource()'s.
  [[nodiscard]] PassHandle declarePass(std::string_view label = {});

  // Declares a read usage of `resource` by `pass`. Declaring the same
  // (pass, resource) read usage more than once is legal and idempotent.
  // `pass`/`resource` must have been vended by this builder instance and
  // still be within range; a default/invalid or foreign-live-builder
  // handle is a programmer error (assertion) -- see this file's
  // check-then-early-return usage below. Declaring a read and a write of
  // the same resource on the same pass is also a programmer error
  // (assertion); this method (and writes(), symmetrically) performs that
  // check via declareUsage(), since only there is both this pass's
  // already-accumulated usage state and the new usage available at the
  // point of the call.
  void reads(PassHandle pass, ResourceHandle resource);

  // Declares a write usage of `resource` by `pass`. Declaring the same
  // (pass, resource) write usage more than once (by this same pass) is
  // legal and idempotent -- it does not count as a second, distinct
  // producer of `resource`; that determination is made later, from the
  // number of *distinct* producing passes, not the number of write
  // usage records. See reads()'s own comment for the same-pass
  // read+write conflict check both methods perform.
  void writes(PassHandle pass, ResourceHandle resource);

  // Plan 0006, ADR-0021: same rules as the untagged reads()/writes()
  // above, additionally tagging the usage with a ResourceState for
  // render_graph::execute()'s transition-insertion algorithm. Additive to
  // Spec 0005's model -- the single-producer rule, dependency derivation,
  // cycle detection, and deterministic ordering (ADR-0017/ADR-0018) are
  // unaffected; the tag participates in transition bookkeeping only,
  // never in dependency derivation or ordering.
  void reads(PassHandle pass, ResourceHandle resource, atlantis::rhi::ResourceState state);
  void writes(PassHandle pass, ResourceHandle resource, atlantis::rhi::ResourceState state);

  // Plan 0006: records `fn` as `pass`'s execution callback, invoked by
  // render_graph::execute() when this pass's turn in the compiled order
  // arrives. A pass with no callback set is legal (Spec 0005 pass
  // retention is unaffected) -- execute() simply invokes nothing for it.
  // `pass` must have been vended by this builder instance and still be
  // within range; a default/invalid or foreign-live-builder handle is a
  // programmer error (assertion), same tier as reads()/writes() above.
  // Calling this more than once for the same pass replaces the
  // previously-set callback.
  void setExecute(PassHandle pass, PassExecuteFn fn);

  // Reads this builder's accumulated state and produces a result; never
  // mutates, consumes, or invalidates this builder, on either success or
  // failure (ADR-0017) -- enforced by the compiler via `const`, not only
  // documented. Repeatable: calling this again on an unmodified builder
  // yields an equivalent result every time (ADR-0018's determinism
  // guarantee).
  //
  // Not yet implemented -- declared only. A later implementation step
  // wires this to the dependency-derivation/compilation algorithm (Plan
  // 0005 Section 6); calling it before that step exists is a link error,
  // not a runtime placeholder.
  [[nodiscard]] atlantis::Result<CompiledGraph, CompileError> compile() const;

 private:
  enum class UsageKind { Read, Write };
  struct ResourceUsage {
    std::size_t resourceIndex;
    UsageKind kind;
    std::optional<atlantis::rhi::ResourceState> state;  // empty for an untagged Spec 0005 usage
  };
  struct PassRecord {
    std::string label;
    std::vector<ResourceUsage> usages;
    PassExecuteFn executeFn;  // empty std::function if setExecute() was never called for this pass
  };
  struct ResourceRecord {
    std::string label;
  };

  // Provenance/validity checks -- see below for the UB-safe
  // check-then-early-return pattern every caller of these follows.
  [[nodiscard]] bool owns(PassHandle handle) const noexcept;
  [[nodiscard]] bool owns(ResourceHandle handle) const noexcept;

  void declareUsage(PassHandle pass, ResourceHandle resource, UsageKind kind,
                     std::optional<atlantis::rhi::ResourceState> state = std::nullopt);

  std::vector<PassRecord> passes_;
  std::vector<ResourceRecord> resources_;
};

}  // namespace atlantis::render_graph

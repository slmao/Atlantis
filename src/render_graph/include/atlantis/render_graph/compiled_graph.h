#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::render_graph {

class RenderGraphBuilder;  // fwd decl; only compile() constructs a CompiledGraph

// A compiled-local identity for a pass, distinct from PassHandle and
// distinct from CompileError's PassDiagnostic::declarationIndex.
// Interpreting one never requires the originating RenderGraphBuilder to
// still be alive (ADR-0017) -- it is meaningful only with respect to,
// and only while, the specific CompiledGraph that vended it is alive and
// has not been the source of a move (see that class's own contract
// below). Not usable as a RenderGraphBuilder declaration handle (a
// different type -- compile-time error, not a runtime check, if
// misused).
//
// Ownership/provenance contract -- consolidated here as the single
// authoritative statement of CompiledPassId's boundary, deliberately
// narrower than PassHandle/ResourceHandle's, because the difference
// matters:
//   - Default-constructed to an always-invalid sentinel (never a real
//     compiled-order position -- see index_ below).
//   - A default-constructed or otherwise out-of-range CompiledPassId,
//     passed to CompiledGraph::passOrder()/label()/dependency(), IS
//     guaranteed-detectable (a programmer-error assertion -- Plan
//     Section 5/8).
//   - A CompiledPassId that was vended by a *different* CompiledGraph,
//     whose index() happens to be in range for the CompiledGraph it is
//     actually passed to, is NOT claimed to be detectable -- this is a
//     graph-scoped identity precondition violation (Spec 0005 Error
//     Model's lifetime-precondition-violation tier, extended to this
//     case), the caller's obligation to avoid, not a case this module
//     tests dynamically.
//   - Dependency endpoints (CompiledDependencyEdge::from/to, below) are
//     always meant to be interpreted against the *same* CompiledGraph
//     that produced the edge -- a CompiledDependencyEdge obtained from
//     one CompiledGraph is never mixed with a CompiledPassId from
//     another.
//   - CompiledPassId carries NO owner pointer/tag of any kind, unlike
//     PassHandle/ResourceHandle, and no generation counter or global
//     registry either. This is a deliberate difference, not an
//     oversight: PassHandle/ResourceHandle's owner-pointer trick is only
//     safe because RenderGraphBuilder is non-movable -- CompiledGraph
//     is, per Spec 0005, at minimum movable, so its address is NOT
//     stable, and tagging a CompiledPassId with "the CompiledGraph's
//     address at vend time" would break the moment that CompiledGraph is
//     moved. Manufacturing a stable identity anyway (a generation
//     counter, a heap-allocated shared control block, a global registry)
//     would be exactly the machinery Spec 0005 forbids for this purpose.
//
// Thread-safety: an ordinary, trivially-copyable value type with no
// internal synchronization and none needed -- safe to copy and pass
// between threads as data. Using it (passing it to a CompiledGraph
// method) is subject to that CompiledGraph's own no-concurrent-access
// contract (below), not an independent restriction.
class CompiledPassId {
 public:
  CompiledPassId() noexcept = default;
  [[nodiscard]] bool operator==(const CompiledPassId&) const noexcept = default;

  // The pass's zero-based position in the compiled execution order that
  // vended it -- this compiled-local identity's own concrete
  // representation, a Plan-stage decision (Spec 0005 leaves the
  // representation open). Returns the invalid sentinel for a default-
  // constructed CompiledPassId.
  [[nodiscard]] std::size_t index() const noexcept { return index_; }

 private:
  friend class CompiledGraph;
  explicit CompiledPassId(std::size_t index) noexcept : index_(index) {}
  std::size_t index_ = static_cast<std::size_t>(-1);  // invalid sentinel, never a real position
};

// A single producer -> reader dependency relation in a compiled graph.
// Both endpoints are meant to be interpreted against the same
// CompiledGraph that produced this edge (see CompiledPassId above).
// Default-constructed to two invalid-sentinel CompiledPassId endpoints --
// see CompiledGraph::dependency()'s assertion fallback.
//
// Ownership: a plain, trivially-copyable aggregate of two CompiledPassId
// values -- owns nothing beyond them, no borrowed pointers.
//
// Thread-safety: same as CompiledPassId above -- safe to copy as data;
// using it against a CompiledGraph is subject to that CompiledGraph's
// own contract.
struct CompiledDependencyEdge {
  CompiledPassId from;  // the producer
  CompiledPassId to;    // the reader
};

// The immutable, independently-owned result of a successful compile()
// (ADR-0017). Owns its own compiled-local pass identity, order, labels,
// and dependency relations -- never borrows the originating
// RenderGraphBuilder's storage, and has no shared/reference-counted state
// of any kind (no two CompiledGraph instances ever share data). The
// originating builder may be destroyed, or may keep accumulating further
// declarations, without affecting any CompiledGraph already produced
// (ADR-0017).
//
// Ownership and move semantics: move-only -- both move construction and
// move assignment are kept (see Plan 0005 Section 4's "Why move
// assignment is kept" for why this was not narrowed to
// construction-only). Full view-invalidation contract, stated once here
// as the authoritative version (label()'s own comment cross-references
// this rather than repeating it):
//   - Moving FROM a CompiledGraph (as the source of either move
//     construction or move assignment) transfers its owned data to the
//     destination; the moved-from source is left in a valid but
//     unspecified state and must not be used for anything other than
//     destruction or re-assignment. Every std::string_view previously
//     obtained from label() on that source instance is invalidated by
//     the move, exactly as if the source had been destroyed.
//   - Moving INTO a CompiledGraph via move ASSIGNMENT (the destination
//     of `a = std::move(b)`) first discards `a`'s own prior owned data --
//     every std::string_view previously obtained from `a.label(...)`,
//     for content `a` held *before* the assignment, is invalidated at
//     that point, for the same reason as any other replaced owned
//     string: the storage it pointed into no longer holds that content.
//     Views obtained from `b` before the assignment remain invalid per
//     the "moving FROM" rule above -- a caller must re-call `label()` on
//     `a` after the assignment to obtain valid views into `a`'s new
//     (formerly `b`'s) content.
//   - Move construction of a new CompiledGraph from an existing one has
//     no effect on any *other*, independent CompiledGraph instance --
//     views into an unrelated CompiledGraph `c` remain valid regardless
//     of what happens to `a`/`b`.
//   - Self-move-assignment (`a = std::move(a)`) is not given any
//     stronger guarantee than the defaulted move-assignment operator
//     provides; a caller must not rely on it leaving `a` unchanged or in
//     any particular state.
//
// Thread-safety: **not declared thread-safe for concurrent access,
// including concurrent reads** (Spec 0005 Proposed Design, "Threading").
// Immutability here is a mutation guarantee (no public method mutates a
// CompiledGraph after construction), not a concurrency guarantee -- this
// module does not reason about, and does not claim, that it is safe to
// call any method (even a `const` one) on the same CompiledGraph
// instance from more than one thread without external synchronization.
// All access is assumed to happen on the single Phase 1 logical frame
// thread (ADR-0004).
class CompiledGraph {
 public:
  CompiledGraph(CompiledGraph&&) noexcept = default;
  CompiledGraph& operator=(CompiledGraph&&) noexcept = default;
  CompiledGraph(const CompiledGraph&) = delete;
  CompiledGraph& operator=(const CompiledGraph&) = delete;
  ~CompiledGraph() = default;

  // Every successfully declared pass appears exactly once, at some
  // position in [0, passCount()) -- no dead-pass/unreferenced-pass
  // culling of any kind (ADR-0018).
  [[nodiscard]] std::size_t passCount() const noexcept;

  // The pass at compiled-order position `position` (< passCount()).
  // Position 0 executes first. Passing position >= passCount() is a
  // programmer error (assertion); the fallback return on a non-
  // terminating handler is a default-constructed (invalid-sentinel)
  // CompiledPassId -- never a value that could be mistaken for position
  // 0 or any other real position, precisely because the sentinel is not
  // 0 (see CompiledPassId above). See Plan Section 5/8.
  [[nodiscard]] CompiledPassId passOrder(std::size_t position) const;

  // `pass`'s diagnostic label, as a view into this CompiledGraph's own
  // owned copy of it (never borrowed from the builder or any caller-
  // supplied string at the time compile() ran -- that copy already
  // happened once, into this CompiledGraph's storage) -- may
  // legitimately be empty.
  //
  // Borrow-lifetime contract (the caller owns nothing here): the
  // returned std::string_view is valid only as long as (a) this
  // CompiledGraph instance is still alive, (b) it has not since been
  // moved FROM (as either move-construction or move-assignment source),
  // and (c) it has not since been the DESTINATION of a move-assignment
  // that replaced its content -- see this class's own header comment for
  // the full move/view contract, including the destination-of-
  // move-assignment and self-move-assignment cases. Destroying this
  // CompiledGraph has the same effect. A caller must not cache a
  // label() view across any of these boundaries; if the label is needed
  // to outlive this CompiledGraph, the caller must copy it into an owned
  // std::string of its own.
  //
  // Passing a CompiledPassId not in range for this CompiledGraph is a
  // programmer error (assertion); the fallback return on a non-
  // terminating handler is an empty std::string_view. Note this fallback
  // is not distinguishable from a legitimately empty label by its return
  // value alone -- acceptable because label() is diagnostic-only, and the
  // fallback path is only reachable with a non-terminating handler
  // installed (i.e. under test), where the test already knows an
  // assertion fired via the handler's own recording, not by inspecting
  // label()'s return. See Plan Section 5/8.
  [[nodiscard]] std::string_view label(CompiledPassId pass) const;

  // The number of producer -> reader dependency relations in this
  // compiled graph -- a read-only representation sufficient to check
  // derived edges (Spec 0005 "Compiled graph output").
  [[nodiscard]] std::size_t dependencyCount() const noexcept;

  // The dependency edge at index `i` (< dependencyCount()). Passing an
  // out-of-range index is a programmer error (assertion); the fallback
  // return on a non-terminating handler is a CompiledDependencyEdge whose
  // `from`/`to` are both the invalid-sentinel CompiledPassId -- never
  // mistakable for a real edge, since no real edge ever has an
  // invalid-sentinel endpoint. See Plan Section 5/8.
  [[nodiscard]] CompiledDependencyEdge dependency(std::size_t i) const;

 private:
  friend class RenderGraphBuilder;
  struct PassRecord {
    std::string label;
  };

  // A dependency edge as two plain compiled-position indices, not two
  // already-constructed CompiledPassId values. This exists because
  // CompiledPassId's constructor is only friended to CompiledGraph
  // itself (see that class's own header comment) -- RenderGraphBuilder
  // is a friend of CompiledGraph, but that does not make it a friend of
  // CompiledPassId, so it has no legal way to construct one directly.
  // Passing raw indices here and constructing the real CompiledPassId
  // values inside this constructor's own body (a CompiledGraph member
  // function, and therefore already permitted) closes that gap without
  // widening any public API: EdgeRecord is private, never returned or
  // accepted by any public method, and CompiledPassId's constructor
  // gains no new friend.
  struct EdgeRecord {
    std::size_t from;
    std::size_t to;
  };

  CompiledGraph(std::vector<PassRecord> passesInOrder, std::vector<EdgeRecord> edges);

  std::vector<PassRecord> passesInOrder_;  // index IS the compiled position
                                            // AND the CompiledPassId value
  std::vector<CompiledDependencyEdge> edges_;
};

}  // namespace atlantis::render_graph

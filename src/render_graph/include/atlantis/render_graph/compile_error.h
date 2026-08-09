#pragma once

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace atlantis::render_graph {

// A single pass's owned diagnostic identity, used inside CompileError.
//
// Ownership: fully independent -- `declarationIndex` is the zero-based
// position among all passes declared on the builder at the time
// compile() was called (a plain integer, not a PassHandle -- Plan
// Section 5 explains why a CompileError never carries a PassHandle), and
// `label` is an owned copy of that pass's diagnostic label (never
// borrowed). Both remain meaningful even after the originating builder is
// destroyed. `label` may be empty, and may duplicate another entry's
// label -- `declarationIndex`, not `label`, is what disambiguates
// entries (Spec 0005 Error Model).
//
// Thread-safety: an ordinary owned value type with no internal
// synchronization; safe to copy, move, or read from any single thread at
// a time, like any other plain data. Not applicable to unsynchronized
// concurrent mutation, which nothing in this module performs on it.
struct PassDiagnostic {
  std::size_t declarationIndex = 0;
  std::string label;
};

// A single logical resource's owned diagnostic identity, used inside
// MultipleProducersError (below). Same ownership/thread-safety contract
// as PassDiagnostic, but `declarationIndex` here is a position in the
// builder's *resource* declarations, never to be confused with a pass's
// position even though the two structs have identical shape -- they
// index different declaration spaces (passes vs. resources).
struct ResourceDiagnostic {
  std::size_t declarationIndex = 0;
  std::string label;
};

// The exact, and only, shape a compile() failure takes when more than
// one pass declared a write usage against the same logical resource.
// `resource` is that resource's own diagnostic identity -- always
// present; there is no state in which this type exists without one.
// `producers` lists every distinct producing pass, ordered by
// declarationIndex -- always at least two entries, since the only code
// path that constructs this type has already confirmed at least two
// distinct producers before doing so.
//
// Ownership: independently owned -- never references the builder that
// produced it (ADR-0017's independent-ownership requirement, applied to
// error payloads as well as the success payload).
//
// Thread-safety: an ordinary owned value type, safe to move/copy/read
// from a single thread at a time; produced once by a single compile()
// call and typically only read thereafter.
struct MultipleProducersError {
  ResourceDiagnostic resource;
  std::vector<PassDiagnostic> producers;
};

// The exact, and only, shape a compile() failure takes when a dependency
// cycle is found among producer-derived edges, with no resource at
// fault. `passes` is the deterministic cycle witness: exactly the passes
// forming one concrete cycle, in canonical traversal order starting from
// its own smallest-declarationIndex member -- never a downstream pass
// that merely depends on the cycle without being part of it, and never
// every pass involved in every cycle if more than one exists. There is
// deliberately no `resource` field anywhere on this type -- not an
// always-empty one -- because no single resource ever identifies a
// cycle.
//
// Ownership/thread-safety: same as MultipleProducersError above.
struct DependencyCycleError {
  std::vector<PassDiagnostic> passes;
};

// The only two ways compile() can fail: a closed sum type, not a
// `{kind, optional-payload}` struct -- see Plan 0005 Section 2's
// rationale for why this shape was chosen instead. Every illegal
// kind/payload combination (a cycle error carrying a resource, or a
// multiple-producers error missing one) is unrepresentable at the type
// level, not merely disallowed by convention.
using CompileError = std::variant<MultipleProducersError, DependencyCycleError>;

}  // namespace atlantis::render_graph

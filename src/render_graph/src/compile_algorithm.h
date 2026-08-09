#pragma once

#include <atlantis/render_graph/compile_error.h>
#include <atlantis/result.h>

#include <cstddef>
#include <string>
#include <vector>

namespace atlantis::render_graph::detail {

// Private, plain-data mirror of what a RenderGraphBuilder has already
// accumulated -- deliberately not the public PassHandle/ResourceHandle
// API, so this algorithm is unit-testable without constructing a full
// RenderGraphBuilder (Plan 0005 Section 6, mirroring plans/0003's
// detail::decideRecreateAction() precedent).
//
// Precondition (enforced by RenderGraphBuilder, not re-checked here):
// every RawUsage::resourceIndex is a valid index into the `resources`
// vector passed to compile() alongside these passes, and no single
// RawPass contains both a Read and a Write usage of the same
// resourceIndex -- RenderGraphBuilder::declareUsage() already rejects
// that combination (ATLANTIS_CHECK) before a usage is ever recorded.
// compile() is a private algorithm operating on already-validated data;
// it does not re-validate these two invariants, and no test exercises it
// with input that violates them (doing so would be exercising undefined
// behavior this module does not write or sanction).
enum class UsageKind { Read, Write };

struct RawUsage {
  std::size_t resourceIndex;
  UsageKind kind;
};

struct RawPass {
  std::string label;
  std::vector<RawUsage> usages;
};

struct RawResource {
  std::string label;
};

// One compiled pass's data, in compiled-position order -- `label` is an
// owned copy (never borrowed from `passes`), ready for
// RenderGraphBuilder::compile() to move into a public CompiledGraph.
// declarationIndex (Plan 0006) is the index into the `passes` vector this
// compiled position came from -- it lets RenderGraphBuilder::compile()
// correlate a compiled position back to that pass's own accumulated
// usages (including ResourceState tags) and execution callback, which
// this purely-structural algorithm has no reason to know about.
struct CompiledPassData {
  std::string label;
  std::size_t declarationIndex;
};

// A single producer -> reader dependency relation, expressed as
// **compiled-position indices** (indices into CompiledGraphData::
// passesInOrder below), not declaration-index positions into `passes`.
// Compiled-position space is chosen deliberately: it is exactly the
// index space CompiledPassId::index() uses (Plan 0005 Section 4), so a
// later implementation step can construct CompiledDependencyEdge values
// directly from these, with no further remapping. This algorithm's own
// internal declaration-index-space edges (Step-by-step, steps 2-3) are
// translated into this space once, at the point compiledOrder is known
// (Step 5) -- see compile_algorithm.cpp's declarationToCompiledPosition
// local mapping.
struct CompiledEdge {
  std::size_t from;
  std::size_t to;
};

// The plain, non-owning-of-a-builder intermediate result of a successful
// compile() -- not itself a public type (Plan 0005 Section 6's own
// callout). RenderGraphBuilder::compile() (a later implementation step)
// is expected to move this into a public CompiledGraph.
struct CompiledGraphData {
  std::vector<CompiledPassData> passesInOrder;  // index IS the compiled position
  std::vector<CompiledEdge> edges;              // compiled-position endpoints
};

// Derives dependency edges and a deterministic pass order from `passes`'
// declared usages against `resources`, or reports exactly why it could
// not (Plan 0005 Section 6's 8-step algorithm; see compile_algorithm.cpp
// for the step-by-step implementation). Never mutates its arguments.
// Deterministic: repeated calls with an unmodified, equal `passes`/
// `resources` yield an equivalent result every time (ADR-0018).
//
// Deviation from Plan 0005's own literal text, recorded here and in this
// implementation step's report: Plan 0005 Section 6 writes this
// function's second parameter as `std::size_t resourceCount`. A resource
// count alone cannot satisfy the already-Approved public contract this
// function's own callers must produce: MultipleProducersError::resource
// (Plan 0005 Section 2, compile_error.h) requires a real
// ResourceDiagnostic, which carries an owned `label` -- not obtainable
// from a bare count. Taking `const std::vector<RawResource>&` instead
// (mirroring RawPass's own shape) is a private data-plumbing correction
// only: it changes no public header, no public type, and no architecture
// decision Spec 0005/ADR-0017/ADR-0018 fixed -- RenderGraphBuilder's own
// (already-implemented) resources_ already stores exactly this
// information, so this is simply passing through what the Approved
// public error contract already requires. This deviation must be
// disclosed in the implementing PR's "Deviations from Plan" section.
[[nodiscard]] atlantis::Result<CompiledGraphData, CompileError> compile(const std::vector<RawPass>& passes,
                                                                         const std::vector<RawResource>& resources);

}  // namespace atlantis::render_graph::detail

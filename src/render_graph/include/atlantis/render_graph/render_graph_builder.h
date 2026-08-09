#pragma once

namespace atlantis::render_graph {

// Forward declaration only. Plan 0005 Section 3's full RenderGraphBuilder
// API (declareResource(), declarePass(), reads(), writes(), compile()) is
// implemented in a later implementation step (Plan Section 12, step 5
// onward) -- this header exists now only so PassHandle/ResourceHandle
// (handles.h) and CompiledGraph (compiled_graph.h)'s `friend class
// RenderGraphBuilder` declarations have a name to refer to.
class RenderGraphBuilder;

}  // namespace atlantis::render_graph

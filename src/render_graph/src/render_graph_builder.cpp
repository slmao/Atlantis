#include <atlantis/render_graph/render_graph_builder.h>

#include <atlantis/assert.h>

#include <string>
#include <utility>
#include <vector>

#include "compile_algorithm.h"

namespace atlantis::render_graph {

ResourceHandle RenderGraphBuilder::declareResource(std::string_view label) {
  const std::size_t index = resources_.size();
  resources_.push_back(ResourceRecord{std::string(label)});
  return ResourceHandle(this, index);
}

PassHandle RenderGraphBuilder::declarePass(std::string_view label) {
  const std::size_t index = passes_.size();
  passes_.push_back(PassRecord{std::string(label), {}});
  return PassHandle(this, index);
}

void RenderGraphBuilder::reads(PassHandle pass, ResourceHandle resource) {
  const bool validPass = owns(pass);
  const bool validResource = owns(resource);
  ATLANTIS_CHECK(validPass);
  ATLANTIS_CHECK(validResource);
  if (!validPass || !validResource) return;
  declareUsage(pass, resource, UsageKind::Read);
}

void RenderGraphBuilder::writes(PassHandle pass, ResourceHandle resource) {
  const bool validPass = owns(pass);
  const bool validResource = owns(resource);
  ATLANTIS_CHECK(validPass);
  ATLANTIS_CHECK(validResource);
  if (!validPass || !validResource) return;
  declareUsage(pass, resource, UsageKind::Write);
}

bool RenderGraphBuilder::owns(PassHandle handle) const noexcept {
  return handle.owner_ == this && handle.index_ < passes_.size();
}

bool RenderGraphBuilder::owns(ResourceHandle handle) const noexcept {
  return handle.owner_ == this && handle.index_ < resources_.size();
}

void RenderGraphBuilder::declareUsage(PassHandle pass, ResourceHandle resource, UsageKind kind) {
  PassRecord& passRecord = passes_[pass.index_];
  const UsageKind opposite = (kind == UsageKind::Read) ? UsageKind::Write : UsageKind::Read;

  bool conflicts = false;
  for (const ResourceUsage& usage : passRecord.usages) {
    if (usage.resourceIndex == resource.index_ && usage.kind == opposite) {
      conflicts = true;
      break;
    }
  }

  const bool noConflict = !conflicts;
  ATLANTIS_CHECK(noConflict);
  if (!noConflict) return;

  passRecord.usages.push_back(ResourceUsage{resource.index_, kind});
}

atlantis::Result<CompiledGraph, CompileError> RenderGraphBuilder::compile() const {
  // Copy this builder's accumulated state into the private, plain-data
  // shape detail::compile() operates on -- never borrowed (each RawPass/
  // RawResource owns its own copy of the label), and never mutating
  // passes_/resources_ (both read-only here, `this` is const).
  std::vector<detail::RawResource> rawResources;
  rawResources.reserve(resources_.size());
  for (const ResourceRecord& resource : resources_) {
    rawResources.push_back(detail::RawResource{resource.label});
  }

  std::vector<detail::RawPass> rawPasses;
  rawPasses.reserve(passes_.size());
  for (const PassRecord& pass : passes_) {
    std::vector<detail::RawUsage> rawUsages;
    rawUsages.reserve(pass.usages.size());
    for (const ResourceUsage& usage : pass.usages) {
      const detail::UsageKind kind = (usage.kind == UsageKind::Read) ? detail::UsageKind::Read : detail::UsageKind::Write;
      rawUsages.push_back(detail::RawUsage{usage.resourceIndex, kind});
    }
    rawPasses.push_back(detail::RawPass{pass.label, std::move(rawUsages)});
  }

  atlantis::Result<detail::CompiledGraphData, CompileError> algorithmResult = detail::compile(rawPasses, rawResources);
  if (algorithmResult.isErr()) {
    // The single algorithm this module has (detail::compile(), Plan 0005
    // Section 6) already produced the exact CompileError this method
    // returns -- moved through unchanged, never re-derived or re-run.
    return atlantis::Result<CompiledGraph, CompileError>::Err(std::move(algorithmResult.error()));
  }

  detail::CompiledGraphData& data = algorithmResult.value();

  // Translate the algorithm's compiled-position output into CompiledGraph's
  // own private record types -- each pass label is moved (not copied a
  // second time; detail::compile() already made its own owned copy), and
  // each dependency endpoint is passed as a plain compiled-position index
  // (CompiledGraph::EdgeRecord), since only CompiledGraph itself may
  // construct a CompiledPassId (see compiled_graph.h's EdgeRecord comment).
  std::vector<CompiledGraph::PassRecord> compiledPasses;
  compiledPasses.reserve(data.passesInOrder.size());
  for (detail::CompiledPassData& passData : data.passesInOrder) {
    compiledPasses.push_back(CompiledGraph::PassRecord{std::move(passData.label)});
  }

  std::vector<CompiledGraph::EdgeRecord> compiledEdges;
  compiledEdges.reserve(data.edges.size());
  for (const detail::CompiledEdge& edge : data.edges) {
    compiledEdges.push_back(CompiledGraph::EdgeRecord{edge.from, edge.to});
  }

  return atlantis::Result<CompiledGraph, CompileError>::Ok(
      CompiledGraph(std::move(compiledPasses), std::move(compiledEdges)));
}

}  // namespace atlantis::render_graph

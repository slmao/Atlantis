#include <atlantis/render_graph/render_graph_builder.h>

#include <atlantis/assert.h>

#include <string>
#include <utility>

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

}  // namespace atlantis::render_graph

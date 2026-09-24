#include <atlantis/renderer/bloom.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <utility>

namespace atlantis::renderer {

std::array<atlantis::rhi::Extent2D, kBloomLevelCount> bloomLevelExtents(atlantis::rhi::Extent2D hdrExtent) {
  std::array<atlantis::rhi::Extent2D, kBloomLevelCount> extents{};
  atlantis::rhi::Extent2D current = hdrExtent;
  for (atlantis::rhi::Extent2D& level : extents) {
    current = {std::max(1U, current.width / 2), std::max(1U, current.height / 2)};
    level = current;
  }
  return extents;
}

atlantis::rhi::HdrColorTarget& BloomTargets::downsampleTarget(std::size_t level) const {
  ATLANTIS_CHECK_MSG(level < downsample_.size(), "BloomTargets::downsampleTarget(): level out of range");
  return *downsample_[level];
}

atlantis::rhi::HdrColorTarget& BloomTargets::upsampleTarget(std::size_t level) const {
  ATLANTIS_CHECK_MSG(level < upsample_.size(), "BloomTargets::upsampleTarget(): level out of range");
  return *upsample_[level];
}

atlantis::Result<BloomTargets, CreateBloomTargetsError> createBloomTargets(atlantis::rhi::Device& device,
                                                                          atlantis::rhi::Extent2D hdrExtent) {
  using ResultT = atlantis::Result<BloomTargets, CreateBloomTargetsError>;
  if (hdrExtent.width == 0 || hdrExtent.height == 0) return ResultT::Err(CreateBloomTargetsError::ZeroExtent);

  // Built into a local bundle and returned only once complete, so a
  // failure part-way releases whatever was already created.
  BloomTargets targets;
  targets.extent_ = hdrExtent;
  const auto levelExtents = bloomLevelExtents(hdrExtent);
  const auto create = [&device](atlantis::rhi::Extent2D extent) -> std::unique_ptr<atlantis::rhi::HdrColorTarget> {
    auto result = device.createHdrColorTarget({.extent = extent});
    if (result.isErr()) return nullptr;
    return std::move(result.value());
  };
  for (std::size_t level = 0; level < kBloomLevelCount; ++level) {
    targets.downsample_[level] = create(levelExtents[level]);
    if (!targets.downsample_[level]) return ResultT::Err(CreateBloomTargetsError::TargetCreationFailed);
  }
  for (std::size_t level = 0; level + 1 < kBloomLevelCount; ++level) {
    targets.upsample_[level] = create(levelExtents[level]);
    if (!targets.upsample_[level]) return ResultT::Err(CreateBloomTargetsError::TargetCreationFailed);
  }
  targets.composite_ = create(hdrExtent);
  if (!targets.composite_) return ResultT::Err(CreateBloomTargetsError::TargetCreationFailed);

  auto samplerResult = device.createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (samplerResult.isErr()) return ResultT::Err(CreateBloomTargetsError::SamplerCreationFailed);
  targets.sampler_ = std::move(samplerResult.value());
  return ResultT::Ok(std::move(targets));
}

}  // namespace atlantis::renderer

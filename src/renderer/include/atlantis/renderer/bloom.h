#pragma once

#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

#include <array>
#include <cstddef>
#include <memory>

namespace atlantis::renderer {

// Spec 0044 / ADR-0092 (Plan 0044 P5): bloom's fixed shape. Six
// downsample levels from half the HDR extent (ruling Q3), and Filament's
// highlight-compression constant for the D1 bright-pass. Neither is
// authored.
inline constexpr std::size_t kBloomLevelCount = 6;
inline constexpr float kBloomHighlight = 1000.0f;

// The extents of D1..D6 for an HDR target of hdrExtent: each dimension
// max(1, floor(previous / 2)), starting from the HDR extent itself. U1..U5
// share D1..D5's extents; the composite is hdrExtent. Pure,
// GPU-independent.
[[nodiscard]] std::array<atlantis::rhi::Extent2D, kBloomLevelCount> bloomLevelExtents(
    atlantis::rhi::Extent2D hdrExtent);

enum class CreateBloomTargetsError {
  ZeroExtent,              // hdrExtent has a zero dimension -- nothing to size from
  TargetCreationFailed,    // any of the twelve createHdrColorTarget() calls failed
  SamplerCreationFailed,
};

// The twelve Rgba16Float HdrColorTargets bloom renders through (ADR-0092
// Decision 3) -- D1..D6, U1..U5 and the full-size composite -- plus the
// linear, clamp-to-edge Sampler every bloom pass reads them with.
//
// Ownership: caller-owned RAII, the Mesh/HdrColorTarget tier. Every
// target is created from, and must be destroyed before, the Device
// passed to createBloomTargets(). The Renderer only borrows it per
// drawFrame() call (via BloomInput); it stays a stateless orchestrator.
// A caller whose HdrColorTarget changes extent replaces the whole bundle
// (createBloomTargets() again) where it recreates the HdrColorTarget.
//
// Thread safety: not thread-safe; used on the frame-orchestration thread
// only, like every RHI resource it holds.
class BloomTargets {
 public:
  BloomTargets(BloomTargets&&) noexcept = default;
  BloomTargets& operator=(BloomTargets&&) noexcept = default;
  BloomTargets(const BloomTargets&) = delete;
  BloomTargets& operator=(const BloomTargets&) = delete;
  ~BloomTargets() = default;

  // The HDR extent the bundle was sized from -- also the composite's.
  [[nodiscard]] atlantis::rhi::Extent2D extent() const { return extent_; }
  // D(level + 1), level in [0, kBloomLevelCount).
  [[nodiscard]] atlantis::rhi::HdrColorTarget& downsampleTarget(std::size_t level) const;
  // U(level + 1), level in [0, kBloomLevelCount - 1).
  [[nodiscard]] atlantis::rhi::HdrColorTarget& upsampleTarget(std::size_t level) const;
  [[nodiscard]] atlantis::rhi::HdrColorTarget& compositeTarget() const { return *composite_; }
  [[nodiscard]] atlantis::rhi::Sampler& sampler() const { return *sampler_; }

 private:
  friend atlantis::Result<BloomTargets, CreateBloomTargetsError> createBloomTargets(atlantis::rhi::Device&,
                                                                                    atlantis::rhi::Extent2D);
  BloomTargets() = default;

  atlantis::rhi::Extent2D extent_;
  std::array<std::unique_ptr<atlantis::rhi::HdrColorTarget>, kBloomLevelCount> downsample_;
  std::array<std::unique_ptr<atlantis::rhi::HdrColorTarget>, kBloomLevelCount - 1> upsample_;
  std::unique_ptr<atlantis::rhi::HdrColorTarget> composite_;
  std::unique_ptr<atlantis::rhi::Sampler> sampler_;
};

// All-or-nothing: on any failure, every target created so far is released
// and nothing is returned -- the resize-branch "keep the old one and retry"
// contract needs exactly that.
[[nodiscard]] atlantis::Result<BloomTargets, CreateBloomTargetsError> createBloomTargets(
    atlantis::rhi::Device& device, atlantis::rhi::Extent2D hdrExtent);

// drawFrame()'s optional bloom input (Plan 0044 P5, ruling O1: a nullable
// pointer to a struct of borrowed references -- the EnvironmentLighting
// shape). Everything is borrowed for the duration of one drawFrame() call.
// The three Pipelines are created by the caller (ruling Q5) from the
// bloom_downsample / bloom_upsample / bloom_composite shader pairs, each
// with hasCameraUniformBinding = false, no depth attachment and an
// HdrFormat::Rgba16Float color format.
//
// Preconditions, enforced by ATLANTIS_CHECK_MSG in drawFrame():
// targets.extent() equals the HdrColorTarget's extent; strength finite
// and in [0, 1]; threshold finite and >= 0. strength == 0 means off.
struct BloomInput {
  BloomTargets& targets;
  atlantis::rhi::Pipeline& downsamplePipeline;
  atlantis::rhi::Pipeline& upsamplePipeline;
  atlantis::rhi::Pipeline& compositePipeline;
  float strength = 0.0f;
  float threshold = 1.0f;
};

// Push-constant blocks of the three bloom shader pairs, 16 bytes each --
// shaders/bloom_*/bloom_*.slang declare the same layouts, and
// atlantis_shader_compiler checks the size.
struct BloomDownsamplePushConstants {
  float sourceTexelSize[2];  // 1 / source extent
  float threshold;           // the D1 bright-pass knee
  float brightPass;          // 1 for D1 (bright-pass + firefly weight), 0 for D2..D6
};
struct BloomUpsamplePushConstants {
  float lowerTexelSize[2];  // 1 / extent of the lower level being upsampled
  float _pad[2];
};
struct BloomCompositePushConstants {
  float bloomTexelSize[2];  // 1 / U1's extent
  float strength;
  float inverseLevelCount;  // 1 / kBloomLevelCount
};
static_assert(sizeof(BloomDownsamplePushConstants) == 16);
static_assert(sizeof(BloomUpsamplePushConstants) == 16);
static_assert(sizeof(BloomCompositePushConstants) == 16);

}  // namespace atlantis::renderer

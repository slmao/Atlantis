#pragma once

#include <atlantis/asset_system/environment_types.h>
#include <atlantis/renderer/environment_lighting.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace atlantis::runtime {

enum class EnvironmentRealizationError {
  PrefilteredTextureCreateFailed,
  DfgTextureCreateFailed,
  EnvironmentSamplerCreateFailed,
  DfgSamplerCreateFailed,
  SpecularStagingBufferCreateFailed,
  DfgStagingBufferCreateFailed,
};

// Runtime-owned, format-independent environment resources. Materials and the
// frame-scoped Renderer view borrow these objects; therefore this aggregate
// must outlive every IBL Material. Caller-thread-only.
struct EnvironmentLightingResources {
  std::unique_ptr<atlantis::rhi::SampledTexture> prefilteredEnvironment;
  std::unique_ptr<atlantis::rhi::Sampler> environmentSampler;
  std::unique_ptr<atlantis::rhi::SampledTexture> dfgLut;
  std::unique_ptr<atlantis::rhi::Sampler> dfgSampler;
  std::array<float, atlantis::asset_system::kEnvironmentShCoefficientCount *
                        atlantis::asset_system::kEnvironmentShComponentCount>
      irradianceSh{};

  [[nodiscard]] atlantis::renderer::EnvironmentLighting borrowedView() const;
};

// Unpublished first-frame bundle. Staging storage remains alive through the
// submission wait; only resources is moved into RuntimeApplication afterward.
struct EnvironmentLightingCandidate {
  EnvironmentLightingResources resources;
  std::unique_ptr<atlantis::rhi::Buffer> specularStagingBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> dfgStagingBuffer;
  std::vector<atlantis::rhi::SampledTextureUploadRegion> specularUploadRegions;
};

[[nodiscard]] atlantis::Result<EnvironmentLightingCandidate, EnvironmentRealizationError>
realizeEnvironmentCandidate(atlantis::rhi::Device& device,
                            const atlantis::asset_system::EnvironmentAssetData& environmentData);

// Records both uploads and their whole-image ShaderRead transitions into the
// caller's existing frame CommandList. It never submits or publishes state.
void recordEnvironmentUploads(atlantis::rhi::CommandList& commandList,
                              EnvironmentLightingCandidate& candidate);

// Writes the fixed 36-float frame-uniform tail. A null source produces the
// no-environment all-zero contribution.
void writeEnvironmentIrradianceSh(
    std::span<float, atlantis::asset_system::kEnvironmentShCoefficientCount *
                         atlantis::asset_system::kEnvironmentShComponentCount>
        destination,
    const std::array<float, atlantis::asset_system::kEnvironmentShCoefficientCount *
                                atlantis::asset_system::kEnvironmentShComponentCount>* source);

}  // namespace atlantis::runtime

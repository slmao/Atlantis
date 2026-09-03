#pragma once

#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>

namespace atlantis::renderer {

// Frame-scoped, non-owning IBL resource view (Spec 0025/P3). The caller
// owns every referenced resource and keeps them alive through drawFrame();
// Renderer retains nothing. Not internally thread-safe; caller-thread-only.
struct EnvironmentLighting {
  const atlantis::rhi::SampledTexture& prefilteredEnvironment;
  const atlantis::rhi::Sampler& environmentSampler;
  const atlantis::rhi::SampledTexture& dfgLut;
  const atlantis::rhi::Sampler& dfgSampler;
};

}  // namespace atlantis::renderer

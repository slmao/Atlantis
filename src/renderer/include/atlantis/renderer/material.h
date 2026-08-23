#pragma once

#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

namespace atlantis::renderer {

// Owns exactly one Pipeline (ADR-0022). Move-only, single-owner.
// Constructed once; the caller's own resize/format-change contract
// (Plan 0007 Section 13) is responsible for destroying and recreating a
// Material's Pipeline when the bound color/depth format changes -- never
// Renderer's job. Not internally thread-safe; caller-thread-only
// (ADR-0004).
//
// sampledTexture/sampler (Spec 0016/D3): borrowed, non-owning, optional --
// nullable raw pointers, not references, because the binding is genuinely
// optional (an untextured Material, like every one that predates this
// Spec, passes neither). Both-or-neither is checked exactly once, in the
// constructor -- there is no setter for either pointer, so there is no
// other entry point where the invariant could be violated, and no rebind
// path of any kind: once constructed, a Material's own texture/sampler
// binding is fixed for its entire lifetime, matching pipeline_'s own
// already-established no-setter shape. Ownership/destruction-order
// contract: the caller-owning composition root -- never Material itself
// -- must keep any SampledTexture/Sampler it passes here alive for at
// least as long as this Material is used in any drawFrame() call, and
// must destroy this Material before destroying either of them.
class Material {
 public:
  explicit Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline,
                     const atlantis::rhi::SampledTexture* sampledTexture = nullptr,
                     const atlantis::rhi::Sampler* sampler = nullptr) noexcept;
  ~Material() = default;

  Material(const Material&) = delete;
  Material& operator=(const Material&) = delete;
  Material(Material&&) noexcept = default;
  Material& operator=(Material&&) noexcept = default;

  [[nodiscard]] atlantis::rhi::Pipeline& pipeline() const noexcept { return *pipeline_; }
  [[nodiscard]] const atlantis::rhi::SampledTexture* sampledTexture() const noexcept { return sampledTexture_; }
  [[nodiscard]] const atlantis::rhi::Sampler* sampler() const noexcept { return sampler_; }

 private:
  std::unique_ptr<atlantis::rhi::Pipeline> pipeline_;
  const atlantis::rhi::SampledTexture* sampledTexture_ = nullptr;  // borrowed, never owned
  const atlantis::rhi::Sampler* sampler_ = nullptr;                // borrowed, never owned
};

enum class CreateMaterialError {
  PipelineCreationFailed,
};

// Convenience free function -- NOT a Renderer method (ADR-0022: Renderer
// never creates a Material). Each call produces a new, independent
// Material -- no cache, no deduplication. sampledTexture/sampler
// (Spec 0016/D3) default to nullptr -- every pre-existing call site
// compiles and behaves unchanged; a caller building a textured Material
// passes both (Material's own constructor enforces both-or-neither).
[[nodiscard]] atlantis::Result<Material, CreateMaterialError> createMaterial(
    atlantis::rhi::Device& device, const atlantis::rhi::PipelineCreateParams& params,
    const atlantis::rhi::SampledTexture* sampledTexture = nullptr, const atlantis::rhi::Sampler* sampler = nullptr);

}  // namespace atlantis::renderer

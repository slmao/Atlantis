#pragma once

#include <array>
#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

namespace atlantis::renderer {

// Plan 0023 Milestone 5 (Spec 0023 D9's own Accepted Correction,
// 2026-08-30): the Renderer-owned, closed discriminator for which
// push-constant payload shape a Material's own Pipeline expects.
// Consumed by Renderer::drawFrame()'s own exhaustive switch (never a
// new DrawItem field) to decide which payload to build and push.
enum class MaterialPushConstantLayout { ObjectToWorldOnly, PbrDirectLit };

enum class MaterialEnvironmentBinding { None, Ibl };

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
// Plan 0023 Milestone 5: pushConstantLayout/baseColorFactor/
// metallicFactor/roughnessFactor are set exactly once, in the
// constructor's own initializer list, and never reassigned anywhere
// else -- "immutable" by encapsulation (no setter), never by a
// const-qualified member, which would silently delete this class's own
// existing `= default` move-constructor/move-assignment operator (the
// real defect Spec 0023 D9's own Accepted Correction closed).
// MaterialEnvironmentBinding (Spec 0025/P3) is likewise immutable by
// encapsulation. None is the compatibility default; Ibl requires the
// caller to supply EnvironmentLighting to every drawFrame() use.
class Material {
 public:
  explicit Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline, MaterialPushConstantLayout pushConstantLayout,
                     const atlantis::rhi::SampledTexture* sampledTexture = nullptr,
                     const atlantis::rhi::Sampler* sampler = nullptr,
                     std::array<float, 4> baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f}, float metallicFactor = 1.0f,
                     float roughnessFactor = 1.0f,
                     MaterialEnvironmentBinding environmentBinding = MaterialEnvironmentBinding::None) noexcept;
  ~Material() = default;

  Material(const Material&) = delete;
  Material& operator=(const Material&) = delete;
  Material(Material&&) noexcept = default;
  Material& operator=(Material&&) noexcept = default;

  [[nodiscard]] atlantis::rhi::Pipeline& pipeline() const noexcept { return *pipeline_; }
  [[nodiscard]] const atlantis::rhi::SampledTexture* sampledTexture() const noexcept { return sampledTexture_; }
  [[nodiscard]] const atlantis::rhi::Sampler* sampler() const noexcept { return sampler_; }
  [[nodiscard]] MaterialPushConstantLayout pushConstantLayout() const noexcept { return pushConstantLayout_; }
  [[nodiscard]] const std::array<float, 4>& baseColorFactor() const noexcept { return baseColorFactor_; }
  [[nodiscard]] float metallicFactor() const noexcept { return metallicFactor_; }
  [[nodiscard]] float roughnessFactor() const noexcept { return roughnessFactor_; }
  [[nodiscard]] MaterialEnvironmentBinding environmentBinding() const noexcept { return environmentBinding_; }

 private:
  std::unique_ptr<atlantis::rhi::Pipeline> pipeline_;
  const atlantis::rhi::SampledTexture* sampledTexture_ = nullptr;  // borrowed, never owned
  const atlantis::rhi::Sampler* sampler_ = nullptr;                // borrowed, never owned
  MaterialPushConstantLayout pushConstantLayout_;
  std::array<float, 4> baseColorFactor_{1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor_ = 1.0f;
  float roughnessFactor_ = 1.0f;
  MaterialEnvironmentBinding environmentBinding_ = MaterialEnvironmentBinding::None;
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
// Plan 0023 Milestone 5: pushConstantLayout/baseColorFactor/
// metallicFactor/roughnessFactor are new, appended trailing parameters
// (never inserted earlier in the list) -- every pre-existing call site
// across tests/examples/fixtures continues to compile and behave
// unchanged, defaulting to MaterialPushConstantLayout::ObjectToWorldOnly
// exactly as today. material_realization.cpp's own kind-dispatched call
// sites pass pushConstantLayout explicitly, never relying on this
// default, per Spec 0023 D9's own Accepted Correction.
// environmentBinding is the final trailing compatibility parameter and
// defaults to None.
[[nodiscard]] atlantis::Result<Material, CreateMaterialError> createMaterial(
    atlantis::rhi::Device& device, const atlantis::rhi::PipelineCreateParams& params,
    const atlantis::rhi::SampledTexture* sampledTexture = nullptr, const atlantis::rhi::Sampler* sampler = nullptr,
    MaterialPushConstantLayout pushConstantLayout = MaterialPushConstantLayout::ObjectToWorldOnly,
    std::array<float, 4> baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f}, float metallicFactor = 1.0f,
    float roughnessFactor = 1.0f,
    MaterialEnvironmentBinding environmentBinding = MaterialEnvironmentBinding::None);

}  // namespace atlantis::renderer

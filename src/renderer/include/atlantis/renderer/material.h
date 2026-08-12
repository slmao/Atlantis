#pragma once

#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/types.h>

namespace atlantis::renderer {

// Owns exactly one Pipeline (ADR-0022). Move-only, single-owner.
// Constructed once; the caller's own resize/format-change contract
// (Plan 0007 Section 13) is responsible for destroying and recreating a
// Material's Pipeline when the bound color/depth format changes -- never
// Renderer's job. Not internally thread-safe; caller-thread-only
// (ADR-0004).
class Material {
 public:
  explicit Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline) noexcept;
  ~Material() = default;

  Material(const Material&) = delete;
  Material& operator=(const Material&) = delete;
  Material(Material&&) noexcept = default;
  Material& operator=(Material&&) noexcept = default;

  [[nodiscard]] atlantis::rhi::Pipeline& pipeline() const noexcept { return *pipeline_; }

 private:
  std::unique_ptr<atlantis::rhi::Pipeline> pipeline_;
};

enum class CreateMaterialError {
  PipelineCreationFailed,
};

// Convenience free function -- NOT a Renderer method (ADR-0022: Renderer
// never creates a Material). Each call produces a new, independent
// Material -- no cache, no deduplication.
[[nodiscard]] atlantis::Result<Material, CreateMaterialError> createMaterial(
    atlantis::rhi::Device& device, const atlantis::rhi::PipelineCreateParams& params);

}  // namespace atlantis::renderer

// Plan 0027 Milestone 9 fix (ADR-0072 D-7): GPU-independent coverage for
// sampledTextureBindingCountFor() -- locks the four real outcomes
// (UnlitTextured/LitTextured always 1; PbrDirectLit 2 without an
// environment, 4 with one) so a future regression to the buggy ternary
// this replaces (which returned 2 for every non-PbrDirectLit-with-
// environment kind, including UnlitTextured/LitTextured) fails here
// first, not only via a real-GPU Pipeline-creation mismatch.
// Plan 0029 Section P15 (ADR-0074): gains a third, hasNormalMap
// parameter -- PbrDirectLit becomes 3/5 (was 2/4) when a normal map is
// present; every other kind stays unaffected by hasNormalMap.

#include <atlantis/runtime/material_realization.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::asset_system::MaterialKind;
using atlantis::runtime::sampledTextureBindingCountFor;

TEST_CASE("sampledTextureBindingCountFor(): UnlitTextured is always 1, regardless of environment or normal map",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::UnlitTextured, false, false) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::UnlitTextured, true, false) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::UnlitTextured, false, true) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::UnlitTextured, true, true) == 1U);
}

TEST_CASE("sampledTextureBindingCountFor(): LitTextured is always 1, regardless of environment or normal map",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::LitTextured, false, false) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::LitTextured, true, false) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::LitTextured, false, true) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::LitTextured, true, true) == 1U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 3 without an environment or normal map (base-color@1, "
          "shadow-map@2, emissive@3)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, false, false) == 3U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 5 with an environment and no normal map (base-color@1, "
          "environment@2, DFG LUT@3, shadow-map@4, emissive@5)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, true, false) == 5U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 4 with a normal map and no environment (base-color@1, "
          "shadow-map@2, normal-map@3, emissive@4)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, false, true) == 4U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 6 with both an environment and a normal map "
          "(base-color@1, environment@2, DFG LUT@3, shadow-map@4, normal-map@5, emissive@6)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, true, true) == 6U);
}

// Plan 0046 Milestone 1 (ADR-0096): the three IBL-only kinds carry the
// emissive slot one past the normal map (or where it would be).
TEST_CASE("sampledTextureBindingCountFor(): PbrClearcoat/PbrSheen/PbrAnisotropic are 4 with an environment (emissive@4) "
          "and 5 with a normal map too (normal-map@4, emissive@5)",
          "[runtime][material_realization][emissive]") {
  for (const MaterialKind kind : {MaterialKind::PbrClearcoat, MaterialKind::PbrSheen, MaterialKind::PbrAnisotropic}) {
    CHECK(sampledTextureBindingCountFor(kind, true, false) == 4U);
    CHECK(sampledTextureBindingCountFor(kind, true, true) == 5U);
  }
}

// Plan 0042 Milestone 1 (Spec 0042 R4/R5, ADR-0090 Decisions 1/3): each
// material alpha mode realizes to its Pipeline blend/depth-write pair and
// the Renderer's own mode.
TEST_CASE("alphaModeRealizationFor(): Opaque and Mask keep blending off and depth write on; Blend blends without "
          "writing depth",
          "[runtime][material_realization][transparency]") {
  using atlantis::asset_system::MaterialAlphaMode;
  using atlantis::rhi::ColorBlendMode;
  using atlantis::runtime::alphaModeRealizationFor;
  using RendererAlphaMode = atlantis::renderer::MaterialAlphaMode;

  const auto opaque = alphaModeRealizationFor(MaterialAlphaMode::Opaque);
  CHECK(opaque.colorBlendMode == ColorBlendMode::Disabled);
  CHECK(opaque.depthWriteEnabled);
  CHECK(opaque.rendererAlphaMode == RendererAlphaMode::Opaque);

  const auto mask = alphaModeRealizationFor(MaterialAlphaMode::Mask);
  CHECK(mask.colorBlendMode == ColorBlendMode::Disabled);
  CHECK(mask.depthWriteEnabled);
  CHECK(mask.rendererAlphaMode == RendererAlphaMode::Mask);

  const auto blend = alphaModeRealizationFor(MaterialAlphaMode::Blend);
  CHECK(blend.colorBlendMode == ColorBlendMode::AlphaBlend);
  CHECK_FALSE(blend.depthWriteEnabled);
  CHECK(blend.rendererAlphaMode == RendererAlphaMode::Blend);

  // Opaque reproduces PipelineCreateParams' own defaults exactly -- the
  // M1 zero-rendering-change guarantee for every existing material.
  const atlantis::rhi::PipelineCreateParams defaults{};
  CHECK(opaque.colorBlendMode == defaults.colorBlendMode);
  CHECK(opaque.depthWriteEnabled == defaults.depthWriteEnabled);
}

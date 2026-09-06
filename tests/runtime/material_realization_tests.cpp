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

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 2 without an environment or normal map (base-color@1, "
          "shadow-map@2)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, false, false) == 2U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 4 with an environment and no normal map (base-color@1, "
          "environment@2, DFG LUT@3, shadow-map@4)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, true, false) == 4U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 3 with a normal map and no environment (base-color@1, "
          "shadow-map@2, normal-map@3)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, false, true) == 3U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 5 with both an environment and a normal map "
          "(base-color@1, environment@2, DFG LUT@3, shadow-map@4, normal-map@5)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, true, true) == 5U);
}

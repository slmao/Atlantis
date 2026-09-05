// Plan 0027 Milestone 9 fix (ADR-0072 D-7): GPU-independent coverage for
// sampledTextureBindingCountFor() -- locks the four real outcomes
// (UnlitTextured/LitTextured always 1; PbrDirectLit 2 without an
// environment, 4 with one) so a future regression to the buggy ternary
// this replaces (which returned 2 for every non-PbrDirectLit-with-
// environment kind, including UnlitTextured/LitTextured) fails here
// first, not only via a real-GPU Pipeline-creation mismatch.

#include <atlantis/runtime/material_realization.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::asset_system::MaterialKind;
using atlantis::runtime::sampledTextureBindingCountFor;

TEST_CASE("sampledTextureBindingCountFor(): UnlitTextured is always 1, with or without an environment",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::UnlitTextured, false) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::UnlitTextured, true) == 1U);
}

TEST_CASE("sampledTextureBindingCountFor(): LitTextured is always 1, with or without an environment",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::LitTextured, false) == 1U);
  CHECK(sampledTextureBindingCountFor(MaterialKind::LitTextured, true) == 1U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 2 without an environment (base-color@1, shadow-map@2)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, false) == 2U);
}

TEST_CASE("sampledTextureBindingCountFor(): PbrDirectLit is 4 with an environment (base-color@1, environment@2, "
          "DFG LUT@3, shadow-map@4)",
          "[runtime][material_realization]") {
  CHECK(sampledTextureBindingCountFor(MaterialKind::PbrDirectLit, true) == 4U);
}

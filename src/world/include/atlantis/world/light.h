#pragma once

#include <atlantis/world/vec3.h>

namespace atlantis::world {

enum class LightKind { Directional, Point };

// See specs/0019-lighting-foundation.md D2, plans/0019-lighting-foundation.md
// P1. No direction/position of its own -- both are re-derived from the
// owning entity's own current world matrix, the one time Runtime ever
// reads them (Spec 0019 D9): normalize(-column2) for Directional,
// column3 (translation) for Point -- the identical formula/sign
// convention Camera's own forward/eye extraction already uses.
struct Light {
  LightKind kind = LightKind::Directional;
  Vec3 color{1.0f, 1.0f, 1.0f};  // each component in [0, 1]
  float intensity = 1.0f;         // finite, >= 0
  float range = 0.0f;             // Point only; ignored for Directional
};

}  // namespace atlantis::world

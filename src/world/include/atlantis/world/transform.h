#pragma once

#include <atlantis/world/vec3.h>

namespace atlantis::world {

// See adr/0050-transform-hierarchy-composition-and-update-model.md's own
// Math contract: localEulerAnglesRadians is (pitch, yaw, roll) about
// (x, y, z), composed as Ry(yaw) * Rx(pitch) * Rz(roll); local matrix is
// T * R * S.
struct Transform {
  Vec3 localPosition{};
  Vec3 localEulerAnglesRadians{};  // pitch (x), yaw (y), roll (z)
  Vec3 localScale{1.0f, 1.0f, 1.0f};
};

}  // namespace atlantis::world

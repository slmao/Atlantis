#pragma once

namespace atlantis::world {

struct Camera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
};

}  // namespace atlantis::world

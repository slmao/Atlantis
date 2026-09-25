#pragma once

#include <atlantis/world/vec3.h>

namespace atlantis::world {

// Plan 0043 P3 (Spec 0043 R6-R7, ADR-0091 Decision 4): the camera's
// height-fog parameters, plain data like exposureCompensationEv. The
// defaults mean fog off (density 0); the value domain is the Asset
// System's cook/decode check, not World's.
struct CameraFog {
  Vec3 color{1.0f, 1.0f, 1.0f};  // linear HDR
  float density = 0.0f;           // per metre at `height`; 0 = off
  float height = 0.0f;            // reference height, metres
  float heightFalloff = 0.0f;     // per metre; 0 = homogeneous
  float maxOpacity = 1.0f;        // in [0, 1]
};

// Plan 0044 P3 (Spec 0044 R4, ADR-0092 Decision 4): the camera's bloom
// parameters, plain data like CameraFog. strength 0 means bloom off; the
// value domain is the Asset System's cook/decode check, not World's.
struct CameraBloom {
  float strength = 0.0f;   // in [0, 1]; 0 = off
  float threshold = 1.0f;  // scene-referred knee, >= 0
};

struct Camera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
  float exposureCompensationEv = 0.0f;
  CameraFog fog;      // Plan 0043: trailing, so existing Camera{...} inits stay valid
  CameraBloom bloom;  // Plan 0044: trailing, likewise
};

}  // namespace atlantis::world

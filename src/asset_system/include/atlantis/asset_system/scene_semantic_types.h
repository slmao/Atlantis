#pragma once

#include <atlantis/asset_system/asset_guid.h>

#include <array>
#include <optional>

namespace atlantis::asset_system::scene {

// Spec 0049 R1 / ADR-0100 D2 (ruling Q6): the authoring scene's semantic
// component types -- what a scene node means, independent of any
// serializer's shape (the Decoded* codec DTOs stay undescribed, ruling Q1).
// Field names follow World's counterparts; defaults equal the Decoded*
// defaults. Described in assetSystemSchema(). Plain values, safe for
// concurrent use.

struct Transform {
  std::array<float, 3> localPosition{0.0f, 0.0f, 0.0f};
  std::array<float, 3> localEulerAnglesRadians{0.0f, 0.0f, 0.0f};  // pitch (x), yaw (y), roll (z)
  std::array<float, 3> localScale{1.0f, 1.0f, 1.0f};
  friend bool operator==(const Transform&, const Transform&) = default;
};

// density 0 means fog off; the other fields are still ordinary values
// (Plan 0049 ruling J4).
struct CameraFog {
  std::array<float, 3> color{1.0f, 1.0f, 1.0f};  // linear HDR
  float density = 0.0f;
  float height = 0.0f;
  float heightFalloff = 0.0f;
  float maxOpacity = 1.0f;
  friend bool operator==(const CameraFog&, const CameraFog&) = default;
};

// strength 0 means bloom off; threshold is still an ordinary value (J4).
struct CameraBloom {
  float strength = 0.0f;
  float threshold = 1.0f;
  friend bool operator==(const CameraBloom&, const CameraBloom&) = default;
};

struct Camera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
  float exposureCompensationEv = 0.0f;
  CameraFog fog;
  CameraBloom bloom;
  friend bool operator==(const Camera&, const Camera&) = default;
};

enum class LightKind { Directional, Point };

struct Light {
  LightKind kind = LightKind::Directional;
  std::array<float, 3> color{1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 0.0f;  // > 0 for Point, 0 for Directional (scene schema)
  friend bool operator==(const Light&, const Light&) = default;
};

// Authoring references are AssetGuids (ADR-0097); the artifact carries their
// AssetId keys as a projection (Spec 0049 R5).
struct Renderable {
  AssetGuid meshAsset;
  std::optional<AssetGuid> materialAsset;
  friend bool operator==(const Renderable&, const Renderable&) = default;
};

}  // namespace atlantis::asset_system::scene

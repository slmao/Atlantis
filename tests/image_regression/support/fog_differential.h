#pragma once

#include "emissive_differential.h"
#include "fog_reference.h"
#include "pixel_diff.h"
#include "tone_mapping_reference.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/camera.h>
#include <atlantis/world/world.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

// Plan 0043 Milestone 2 (Spec 0043 R1-R3, P8/P9): shared helpers for the
// height-fog GPU tests. Header-only and templated on the fixture, like
// emissive_differential.h -- every PBR fixture exposes the same
// std::optional<World> world member, and the fog lives on its active
// camera, which the fixtures copy into the FogData tail every render.
//
// The saturating-fog differential (P9): the same scene rendered with fog
// off, then with a fog so dense that every fogged fragment is exactly the
// fog colour (f = maxOpacity = 1, and lit * 0 = 0 for finite HDR). Every
// pixel must then either be the fog colour (a PBR fragment) or be
// byte-identical to the fog-off frame (clear colour, sky, and anything no
// PBR shader draws) -- with nothing in between. A PBR variant that skipped
// the fog term would leave its sphere unchanged, so each test also
// requires one known sphere of that variant to turn into the fog colour.

namespace atlantis::image_regression {

using FogRgb = std::array<int, 3>;

[[nodiscard]] inline FogRgb encodeFogRgb(const FogVec3& linear) {
  FogRgb out{};
  for (std::size_t c = 0; c < 3; ++c) {
    out[c] = static_cast<int>(std::lround(tonemapAndEncodeUnorm(linear[c]) * 255.0f));
  }
  return out;
}

[[nodiscard]] inline FogRgb fogPixelRgb(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2]};
}

[[nodiscard]] inline bool fogRgbWithinOneLsb(const FogRgb& a, const FogRgb& b) {
  return std::abs(a[0] - b[0]) <= 1 && std::abs(a[1] - b[1]) <= 1 && std::abs(a[2] - b[2]) <= 1;
}

[[nodiscard]] inline FogParams toFogParams(const atlantis::world::CameraFog& fog) {
  FogParams params;
  params.color = {fog.color.x, fog.color.y, fog.color.z};
  params.density = fog.density;
  params.height = fog.height;
  params.heightFalloff = fog.heightFalloff;
  params.maxOpacity = fog.maxOpacity;
  return params;
}

template <typename Fixture>
[[nodiscard]] atlantis::world::CameraFog activeCameraFog(Fixture& fixture) {
  auto& world = *fixture.world;
  const auto activeCamera = world.activeCamera();
  REQUIRE(activeCamera.has_value());
  const auto camera = world.getCamera(*activeCamera);
  REQUIRE(camera.isOk());
  return camera.value().fog;
}

// Replaces the active camera's fog; takes effect from the next render.
template <typename Fixture>
void setActiveCameraFog(Fixture& fixture, const atlantis::world::CameraFog& fog) {
  auto& world = *fixture.world;
  const auto activeCamera = world.activeCamera();
  REQUIRE(activeCamera.has_value());
  const auto camera = world.getCamera(*activeCamera);
  REQUIRE(camera.isOk());
  atlantis::world::Camera updated = camera.value();
  updated.fog = fog;
  REQUIRE(world.setCamera(*activeCamera, updated).isOk());
}

// Dense enough that 1 - e^-tau is exactly 1 in float at any visible
// distance; a vivid green no clear colour or sky pixel in these scenes is.
[[nodiscard]] inline atlantis::world::CameraFog saturatingFog() {
  atlantis::world::CameraFog fog;
  fog.color = {0.0f, 3.0f, 0.0f};
  fog.density = 1000.0f;
  fog.height = 0.0f;
  fog.heightFalloff = 0.0f;
  fog.maxOpacity = 1.0f;
  return fog;
}

struct SaturatedFogDifferential {
  std::size_t fogColourPixels = 0;  // RGB within 1 LSB of the fog colour, alpha unchanged
  std::size_t unchangedPixels = 0;  // byte-identical (RGBA) to the fog-off frame
  std::size_t otherPixels = 0;      // anything else -- must be 0
};

[[nodiscard]] inline SaturatedFogDifferential compareSaturatedFog(const PixelBuffer& off, const PixelBuffer& on,
                                                                  const FogRgb& fogColour) {
  SaturatedFogDifferential result;
  for (std::uint32_t y = 0; y < off.height; ++y) {
    for (std::uint32_t x = 0; x < off.width; ++x) {
      const std::size_t offset = (static_cast<std::size_t>(y) * off.width + x) * 4;
      bool identical = true;
      for (std::size_t c = 0; c < 4; ++c) identical = identical && on.rgba8[offset + c] == off.rgba8[offset + c];
      if (identical) {
        ++result.unchangedPixels;
      } else if (on.rgba8[offset + 3] == off.rgba8[offset + 3] && fogRgbWithinOneLsb(fogPixelRgb(on, x, y), fogColour)) {
        ++result.fogColourPixels;
      } else {
        ++result.otherPixels;
      }
    }
  }
  return result;
}

// P9 for one shader variant: renders the fixture fog-off, then with the
// saturating fog, and checks the differential above plus the centre of
// the (unit-radius) sphere drawn with `materialLogicalPath`, which the
// caller picks as one realized with the variant under test.
template <typename Fixture, typename RenderFn>
void checkSaturatedFogDifferential(Fixture& fixture, RenderFn render, const char* materialLogicalPath) {
  INFO("probe material: " << materialLogicalPath);
  REQUIRE(activeCameraFog(fixture).density == 0.0f);  // the scene itself declares no fog
  auto offResult = render(fixture);
  REQUIRE(offResult.isOk());
  const PixelBuffer off = std::move(offResult.value());

  setActiveCameraFog(fixture, saturatingFog());
  auto onResult = render(fixture);
  REQUIRE(onResult.isOk());
  const PixelBuffer& on = onResult.value();

  const FogRgb fogColour = encodeFogRgb(toFogParams(saturatingFog()).color);
  const SaturatedFogDifferential report = compareSaturatedFog(off, on, fogColour);
  INFO("fog-colour pixels " << report.fogColourPixels << ", unchanged " << report.unchangedPixels << ", other "
                            << report.otherPixels);
  CHECK(report.otherPixels == 0);
  CHECK(report.fogColourPixels > 1000);
  CHECK(report.unchangedPixels > 1000);  // clear colour / sky pixels, byte-identical

  const auto sphere =
      projectMaterialSphere(fixture, atlantis::asset_system::computeAssetId(materialLogicalPath), 1.0f, on.width);
  REQUIRE(sphere.has_value());
  const auto x = static_cast<std::uint32_t>(sphere->centerX);
  const auto y = static_cast<std::uint32_t>(sphere->centerY);
  const FogRgb before = fogPixelRgb(off, x, y);
  const FogRgb after = fogPixelRgb(on, x, y);
  INFO("sphere centre (" << x << ", " << y << "): off " << before[0] << "," << before[1] << "," << before[2]
                         << " -> on " << after[0] << "," << after[1] << "," << after[2]);
  CHECK_FALSE(fogRgbWithinOneLsb(before, fogColour));
  CHECK(fogRgbWithinOneLsb(after, fogColour));
}

struct FogRayHit {
  FogVec3 eye;
  FogVec3 point;
};

// The world-space point a pixel centre sees on a sphere of `worldRadius`
// (times the node's uniform scale) drawn with `materialAsset`: the ray
// from the active camera through (x + 0.5, y + 0.5), inverted through the
// same extractCameraMatrices() pair the fixtures upload (and the same
// pixel mapping projectMaterialSphere() uses), intersected with the ideal
// sphere. The mesh is a tessellation of it, so its surface lies within a
// few thousandths of a unit of this point.
template <typename Fixture>
[[nodiscard]] std::optional<FogRayHit> sphereHitAtPixel(Fixture& fixture,
                                                        atlantis::asset_system::AssetId materialAsset,
                                                        float worldRadius, std::uint32_t x, std::uint32_t y,
                                                        std::uint32_t extentPixels) {
  using atlantis::runtime::Mat4;
  auto& world = *fixture.world;
  const auto activeCamera = world.activeCamera();
  if (!activeCamera.has_value()) return std::nullopt;
  const auto cameraWorld = world.getWorldMatrix(*activeCamera);
  const auto camera = world.getCamera(*activeCamera);
  if (cameraWorld.isErr() || camera.isErr()) return std::nullopt;
  const auto matrices = atlantis::runtime::extractCameraMatrices(cameraWorld.value(), camera.value().fovYRadians,
                                                                 camera.value().nearZ, camera.value().farZ, 1.0f);
  if (matrices.isErr()) return std::nullopt;
  const Mat4& v = matrices.value().view;
  const Mat4& p = matrices.value().projection;
  // A plain perspective: no x/y shear or cross terms, w = -z_view * p[11].
  if (p[1] != 0.0f || p[4] != 0.0f || p[3] != 0.0f || p[7] != 0.0f || p[12] != 0.0f || p[13] != 0.0f ||
      p[15] != 0.0f) {
    return std::nullopt;
  }

  std::optional<Mat4> objectToWorld;
  for (const auto entity : world.renderableEntities()) {
    const auto renderable = world.getRenderable(entity);
    if (renderable.isErr() || !renderable.value().materialAsset.has_value()) continue;
    if (*renderable.value().materialAsset != materialAsset) continue;
    const auto worldMatrix = world.getWorldMatrix(entity);
    if (worldMatrix.isErr()) return std::nullopt;
    objectToWorld = worldMatrix.value();
    break;
  }
  if (!objectToWorld.has_value()) return std::nullopt;

  // View-space direction through the pixel centre, at z_view = -1.
  const float extent = static_cast<float>(extentPixels);
  const float ndcX = (static_cast<float>(x) + 0.5f) / extent * 2.0f - 1.0f;
  const float ndcY = (static_cast<float>(y) + 0.5f) / extent * 2.0f - 1.0f;
  const float w = -p[11];
  const float viewX = (ndcX * w + p[8]) / p[0];
  const float viewY = (ndcY * w + p[9]) / p[5];
  const float viewZ = -1.0f;
  // World direction = transpose(view rotation) * view direction.
  FogVec3 dir{};
  for (std::size_t j = 0; j < 3; ++j) dir[j] = v[j * 4 + 0] * viewX + v[j * 4 + 1] * viewY + v[j * 4 + 2] * viewZ;
  const float dirLength = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
  for (float& component : dir) component /= dirLength;

  const Mat4& cam = cameraWorld.value();
  const FogVec3 eye{cam[12], cam[13], cam[14]};
  const Mat4& m = *objectToWorld;
  const float radius = worldRadius * std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  const FogVec3 toEye{eye[0] - m[12], eye[1] - m[13], eye[2] - m[14]};
  const float b = toEye[0] * dir[0] + toEye[1] * dir[1] + toEye[2] * dir[2];
  const float c = toEye[0] * toEye[0] + toEye[1] * toEye[1] + toEye[2] * toEye[2] - radius * radius;
  const float discriminant = b * b - c;
  if (discriminant < 0.0f) return std::nullopt;
  const float t = -b - std::sqrt(discriminant);
  if (t <= 0.0f) return std::nullopt;
  return FogRayHit{eye, {eye[0] + t * dir[0], eye[1] + t * dir[1], eye[2] + t * dir[2]}};
}

}  // namespace atlantis::image_regression

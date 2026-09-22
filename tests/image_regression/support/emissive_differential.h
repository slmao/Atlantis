#pragma once

#include "pixel_diff.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/world.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

// Plan 0041 Milestone 3 (Spec 0041 R6, rulings O5/Q4): shared helpers for
// the emissive tests. Header-only because the projection is templated on
// the fixture type -- every PBR fixture exposes the same
// std::optional<World> world member, and nothing else is needed from it.
//
// The on/off differential: the same scene rendered twice, once with one
// material's emissiveFactor set. Emissive is added after all lighting and
// lights nothing else (ADR-0089 Decision 3), so every pixel must either be
// byte-identical or be no darker in any channel, and every changed pixel
// must lie on that material's own sphere.

namespace atlantis::image_regression {

struct ScreenCircle {
  float centerX = 0.0f;
  float centerY = 0.0f;
  float radius = 0.0f;
};

// Projects the (first) sphere drawn with materialAsset to a screen-space
// circle: its world-space centre, and a radius that conservatively bounds
// the sphere's silhouette (the projected offset of a point `worldRadius`
// away along the camera's up axis, times 1.25 for the silhouette's
// perspective widening off-axis, plus 2 pixels for rasterization). Must
// run after the fixture's first render, once World::updateTransforms()
// has run.
template <typename Fixture>
[[nodiscard]] std::optional<ScreenCircle> projectMaterialSphere(Fixture& fixture,
                                                                atlantis::asset_system::AssetId materialAsset,
                                                                float worldRadius, std::uint32_t extentPixels) {
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

  const auto toPixel = [&](float x, float y, float z) -> std::optional<std::pair<float, float>> {
    const Mat4& v = matrices.value().view;
    const Mat4& p = matrices.value().projection;
    // Column-major: column c occupies [c*4, c*4+3].
    const float vx = v[0] * x + v[4] * y + v[8] * z + v[12];
    const float vy = v[1] * x + v[5] * y + v[9] * z + v[13];
    const float vz = v[2] * x + v[6] * y + v[10] * z + v[14];
    const float vw = v[3] * x + v[7] * y + v[11] * z + v[15];
    const float cx = p[0] * vx + p[4] * vy + p[8] * vz + p[12] * vw;
    const float cy = p[1] * vx + p[5] * vy + p[9] * vz + p[13] * vw;
    const float cw = p[3] * vx + p[7] * vy + p[11] * vz + p[15] * vw;
    if (cw <= 0.0f) return std::nullopt;
    const float extent = static_cast<float>(extentPixels);
    return std::make_pair((cx / cw * 0.5f + 0.5f) * extent, (cy / cw * 0.5f + 0.5f) * extent);
  };

  const Mat4& m = *objectToWorld;
  const float scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  const float centreX = m[12], centreY = m[13], centreZ = m[14];
  const auto centre = toPixel(centreX, centreY, centreZ);
  // The camera's own up axis is column 1 of its world matrix.
  const Mat4& cam = cameraWorld.value();
  const float r = worldRadius * scale;
  const auto edge = toPixel(centreX + cam[4] * r, centreY + cam[5] * r, centreZ + cam[6] * r);
  if (!centre.has_value() || !edge.has_value()) return std::nullopt;
  const float dx = edge->first - centre->first;
  const float dy = edge->second - centre->second;
  return ScreenCircle{centre->first, centre->second, std::sqrt(dx * dx + dy * dy) * 1.25f + 2.0f};
}

struct EmissiveDifferential {
  std::size_t changedInside = 0;   // pixels on the sphere that changed
  std::size_t changedOutside = 0;  // pixels anywhere else that changed -- must be 0
  std::size_t darkerPixels = 0;    // pixels with any channel darker -- must be 0
};

[[nodiscard]] inline EmissiveDifferential compareEmissiveOnOff(const PixelBuffer& off, const PixelBuffer& on,
                                                               const ScreenCircle& sphere) {
  EmissiveDifferential result;
  for (std::uint32_t y = 0; y < off.height; ++y) {
    for (std::uint32_t x = 0; x < off.width; ++x) {
      const std::size_t offset = (static_cast<std::size_t>(y) * off.width + x) * 4;
      bool changed = false;
      bool darker = false;
      for (std::size_t c = 0; c < 3; ++c) {
        if (on.rgba8[offset + c] != off.rgba8[offset + c]) changed = true;
        if (on.rgba8[offset + c] < off.rgba8[offset + c]) darker = true;
      }
      if (darker) ++result.darkerPixels;
      if (!changed) continue;
      const float dx = (static_cast<float>(x) + 0.5f) - sphere.centerX;
      const float dy = (static_cast<float>(y) + 0.5f) - sphere.centerY;
      if (dx * dx + dy * dy <= sphere.radius * sphere.radius) {
        ++result.changedInside;
      } else {
        ++result.changedOutside;
      }
    }
  }
  return result;
}

struct EmissiveOnOffResult {
  EmissiveDifferential differential;
  ScreenCircle sphere;
  std::size_t blueChangedPixels = 0;  // the probe's blue component is 0, so must be 0
};

// The complete on/off procedure for one material in one existing scene:
// two independent fixtures from the same config; the "on" fixture's
// materialDataMap entry gets kEmissiveProbe before its first render (the
// fixtures realize materials inside their render call); both frames are
// rendered and compared against that material's projected sphere.
inline constexpr float kEmissiveProbe[3] = {0.6f, 0.3f, 0.0f};

template <typename SetUpFn, typename RenderFn>
[[nodiscard]] EmissiveOnOffResult runEmissiveOnOff(SetUpFn setUp, RenderFn render,
                                                   std::string_view materialLogicalPath) {
  const atlantis::asset_system::AssetId material = atlantis::asset_system::computeAssetId(materialLogicalPath);
  auto offFixture = setUp();
  auto onFixture = setUp();
  REQUIRE(offFixture.isOk());
  REQUIRE(onFixture.isOk());
  auto entry = onFixture.value().materialDataMap.find(material);
  REQUIRE(entry != onFixture.value().materialDataMap.end());
  for (int c = 0; c < 3; ++c) entry->second.emissiveFactor[c] = kEmissiveProbe[c];

  auto off = render(offFixture.value());
  auto on = render(onFixture.value());
  REQUIRE(off.isOk());
  REQUIRE(on.isOk());
  REQUIRE(off.value().width == on.value().width);
  const auto sphere = projectMaterialSphere(onFixture.value(), material, 1.0f, on.value().width);
  REQUIRE(sphere.has_value());

  EmissiveOnOffResult result;
  result.sphere = *sphere;
  result.differential = compareEmissiveOnOff(off.value(), on.value(), *sphere);
  for (std::size_t i = 2; i < off.value().rgba8.size(); i += 4) {
    if (off.value().rgba8[i] != on.value().rgba8[i]) ++result.blueChangedPixels;
  }
  REQUIRE(offFixture.value().device->waitIdle().isOk());
  REQUIRE(onFixture.value().device->waitIdle().isOk());
  return result;
}

// The assertion every on/off test makes: brighter only, only on the
// sphere, and exactly nothing added where the emissive component is 0.
inline void checkEmissiveOnlyBrightensItsSphere(const EmissiveOnOffResult& result) {
  INFO("changedInside=" << result.differential.changedInside << " changedOutside="
                        << result.differential.changedOutside << " darker=" << result.differential.darkerPixels
                        << " blueChanged=" << result.blueChangedPixels << " sphereRadiusPx=" << result.sphere.radius);
  CHECK(result.differential.changedInside > 200);
  // The circle must be a meaningful bound, not a vacuous one: a
  // projection error that inflated it would make changedOutside == 0
  // trivially true. The sphere's own changed pixels fill ~60% of the
  // deliberately generous (1.25x + 2 px) circle; demand at least 30%.
  const double circleArea = 3.14159265358979 * static_cast<double>(result.sphere.radius) * result.sphere.radius;
  CHECK(static_cast<double>(result.differential.changedInside) >= 0.3 * circleArea);
  CHECK(result.differential.changedOutside == 0);
  CHECK(result.differential.darkerPixels == 0);
  CHECK(result.blueChangedPixels == 0);
}

}  // namespace atlantis::image_regression

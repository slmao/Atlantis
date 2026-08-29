#include <atlantis/runtime/scene_extraction.h>

#include <atlantis/assert.h>
#include <atlantis/world/light.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::checkConformalTransform;
using atlantis::runtime::computeLambertianDiffuse;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::extractFrameLightingData;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::LightExtractionInput;
using atlantis::runtime::Mat4;
using atlantis::runtime::resolveMaterialAsset;
using atlantis::runtime::resolveMeshAsset;
using atlantis::runtime::SceneExtractionError;
using atlantis::runtime::Vec3;
using atlantis::world::Light;
using atlantis::world::LightKind;

namespace {
constexpr float kEpsilon = 1e-4f;

struct V3 {
  float x, y, z;
};

[[nodiscard]] float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] V3 cross(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] float length(const V3& v) { return std::sqrt(dot(v, v)); }

// Columns 0/1/2 of a column-major view matrix, as 3-vectors (rows 0-2 only).
[[nodiscard]] V3 col(const Mat4& m, int c) {
  const std::size_t base = static_cast<std::size_t>(c) * 4;
  return {m[base], m[base + 1], m[base + 2]};
}

// A view basis is orthonormal and proper (determinant +1, non-reflected)
// when its three columns are mutually orthogonal, unit-length, and form
// a right-handed triple.
void requireOrthonormalProperBasis(const Mat4& view) {
  const V3 c0 = col(view, 0);
  const V3 c1 = col(view, 1);
  const V3 c2 = col(view, 2);

  REQUIRE(std::abs(length(c0) - 1.0f) < kEpsilon);
  REQUIRE(std::abs(length(c1) - 1.0f) < kEpsilon);
  REQUIRE(std::abs(length(c2) - 1.0f) < kEpsilon);
  REQUIRE(std::abs(dot(c0, c1)) < kEpsilon);
  REQUIRE(std::abs(dot(c0, c2)) < kEpsilon);
  REQUIRE(std::abs(dot(c1, c2)) < kEpsilon);

  const float determinant = dot(c0, cross(c1, c2));
  REQUIRE(std::abs(determinant - 1.0f) < kEpsilon);
}
}  // namespace

// V15 (camera-matrix extraction, well-formed case)
TEST_CASE("extractCameraMatrices(): a well-formed input produces an orthonormal view basis and a correct projection",
          "[runtime][scene_extraction]") {
  // Camera at (0,0,5), no rotation -- forward = -column2 = (0,0,-1).
  const Mat4 cameraWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 5, 1};

  const auto result = extractCameraMatrices(cameraWorld, 1.0f, 0.1f, 100.0f, 1.5f);
  REQUIRE(result.isOk());
  requireOrthonormalProperBasis(result.value().view);

  const Mat4& projection = result.value().projection;
  REQUIRE(std::abs(projection[11] - (-1.0f)) < kEpsilon);  // Vulkan clip-space convention
}

// V15 (the V8 shear-producing configuration, reproduced exactly):
// eye/forward-only extraction stays robust even though this camera
// world matrix's own columns 0/1 are not orthogonal (a parent scaled
// (2,1,1) composed with a 45-degree Z rotation).
TEST_CASE("extractCameraMatrices(): stays robust against the V8 shear-producing world matrix",
          "[runtime][scene_extraction]") {
  constexpr float kSqrtHalf = 0.70710678f;
  const Mat4 shearedCameraWorld{2.0f * kSqrtHalf, kSqrtHalf, 0, 0,  // column 0 (sheared)
                                 -2.0f * kSqrtHalf, kSqrtHalf, 0, 0,  // column 1 (sheared)
                                 0, 0, 1, 0,                          // column 2 (forward source, untouched)
                                 0, 0, 0, 1};                         // column 3 (eye, untouched)

  const auto result = extractCameraMatrices(shearedCameraWorld, 1.0f, 0.1f, 100.0f, 1.0f);
  REQUIRE(result.isOk());
  requireOrthonormalProperBasis(result.value().view);
}

// V15 (negatively-scaled ancestor): a mirrored (negative-determinant)
// source matrix still produces a proper, non-reflected view basis --
// lookAtMatrix() always builds its own basis via cross products,
// independent of what the source matrix's own other columns contained.
TEST_CASE("extractCameraMatrices(): a negatively-scaled ancestor still produces a proper view basis",
          "[runtime][scene_extraction]") {
  const Mat4 mirroredCameraWorld{-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 5, 1};

  const auto result = extractCameraMatrices(mirroredCameraWorld, 1.0f, 0.1f, 100.0f, 1.0f);
  REQUIRE(result.isOk());
  requireOrthonormalProperBasis(result.value().view);
}

// V15 (degenerate case 1)
TEST_CASE("extractCameraMatrices(): a near-zero-length forward returns Err(DegenerateCameraForward)",
          "[runtime][scene_extraction]") {
  // column 2 is (near-)zero -- forward's own pre-normalization length is
  // below the epsilon.
  const Mat4 degenerateCameraWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1e-9f, 0, 0, 0, 0, 1};

  const auto result = extractCameraMatrices(degenerateCameraWorld, 1.0f, 0.1f, 100.0f, 1.0f);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::DegenerateCameraForward);
}

// V15 (degenerate case 2)
TEST_CASE("extractCameraMatrices(): a forward parallel to world-up returns Err(DegenerateCameraBasis)",
          "[runtime][scene_extraction]") {
  // column 2 = (0,-1,0) -- forward = -column2 = (0,1,0), exactly the
  // canonical world-up axis.
  const Mat4 degenerateCameraWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1};

  const auto result = extractCameraMatrices(degenerateCameraWorld, 1.0f, 0.1f, 100.0f, 1.0f);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::DegenerateCameraBasis);
}

// resolveMeshAsset()
TEST_CASE("resolveMeshAsset(): a member AssetId returns Ok()", "[runtime][scene_extraction]") {
  const auto result = resolveMeshAsset(42, {42});
  REQUIRE(result.isOk());
}

TEST_CASE("resolveMeshAsset(): a member AssetId among several returns Ok()", "[runtime][scene_extraction]") {
  const auto result = resolveMeshAsset(2, {1, 2, 3});
  REQUIRE(result.isOk());
}

TEST_CASE("resolveMeshAsset(): a non-member AssetId returns Err(UnresolvedMeshAsset)",
          "[runtime][scene_extraction]") {
  const auto result = resolveMeshAsset(1, {2});
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::UnresolvedMeshAsset);
}

TEST_CASE("resolveMeshAsset(): an empty known set returns Err(UnresolvedMeshAsset)", "[runtime][scene_extraction]") {
  const auto result = resolveMeshAsset(1, {});
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::UnresolvedMeshAsset);
}

// resolveMaterialAsset() -- Plan 0018 Section P9, verbatim-shaped after
// resolveMeshAsset()'s own coverage.
TEST_CASE("resolveMaterialAsset(): a member AssetId returns Ok()", "[runtime][scene_extraction][material]") {
  const auto result = resolveMaterialAsset(42, {42});
  REQUIRE(result.isOk());
}

TEST_CASE("resolveMaterialAsset(): a member AssetId among several returns Ok()",
          "[runtime][scene_extraction][material]") {
  const auto result = resolveMaterialAsset(2, {1, 2, 3});
  REQUIRE(result.isOk());
}

TEST_CASE("resolveMaterialAsset(): a non-member AssetId returns Err(UnresolvedMaterialAsset)",
          "[runtime][scene_extraction][material]") {
  const auto result = resolveMaterialAsset(1, {2});
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::UnresolvedMaterialAsset);
}

TEST_CASE("resolveMaterialAsset(): an empty known set returns Err(UnresolvedMaterialAsset)",
          "[runtime][scene_extraction][material]") {
  const auto result = resolveMaterialAsset(1, {});
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::UnresolvedMaterialAsset);
}

// ---------------------------------------------------------------------
// extractFrameLightingData() -- Plan 0019 Section P8, Milestone 7.
// ---------------------------------------------------------------------

TEST_CASE("extractFrameLightingData(): a single Directional light is extracted via the identical "
          "-column2/normalize formula extractCameraMatrices() uses",
          "[runtime][scene_extraction][light]") {
  // Identity world matrix -- column 2 = (0,0,1), so direction = -column2 =
  // (0,0,-1), matching extractCameraMatrices()'s own well-formed-case test
  // above verbatim in shape.
  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Light light;
  light.kind = LightKind::Directional;
  light.color = {0.2f, 0.4f, 0.6f};
  light.intensity = 2.0f;

  const auto result = extractFrameLightingData({LightExtractionInput{light, identity}});
  REQUIRE(result.isOk());
  const FrameLightingData& data = result.value();
  REQUIRE(data.directionalLightCount == 1);
  REQUIRE(data.pointLightCount == 0);
  REQUIRE(std::abs(data.directionalLights[0].direction[0] - 0.0f) < kEpsilon);
  REQUIRE(std::abs(data.directionalLights[0].direction[1] - 0.0f) < kEpsilon);
  REQUIRE(std::abs(data.directionalLights[0].direction[2] - (-1.0f)) < kEpsilon);
  REQUIRE(std::abs(data.directionalLights[0].color[0] - 0.2f) < kEpsilon);
  REQUIRE(std::abs(data.directionalLights[0].color[1] - 0.4f) < kEpsilon);
  REQUIRE(std::abs(data.directionalLights[0].color[2] - 0.6f) < kEpsilon);
  REQUIRE(std::abs(data.directionalLights[0].intensity - 2.0f) < kEpsilon);
}

TEST_CASE("extractFrameLightingData(): a single Point light's position is column 3 (translation), verbatim",
          "[runtime][scene_extraction][light]") {
  const Mat4 translated{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1};
  Light light;
  light.kind = LightKind::Point;
  light.color = {1.0f, 0.0f, 0.0f};
  light.intensity = 3.0f;
  light.range = 10.0f;

  const auto result = extractFrameLightingData({LightExtractionInput{light, translated}});
  REQUIRE(result.isOk());
  const FrameLightingData& data = result.value();
  REQUIRE(data.directionalLightCount == 0);
  REQUIRE(data.pointLightCount == 1);
  REQUIRE(std::abs(data.pointLights[0].position[0] - 3.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[0].position[1] - 4.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[0].position[2] - 5.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[0].range - 10.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[0].color[0] - 1.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[0].intensity - 3.0f) < kEpsilon);
}

TEST_CASE("extractFrameLightingData(): one Directional plus multiple Point lights preserves the "
          "caller-supplied order, never reordering",
          "[runtime][scene_extraction][light]") {
  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Mat4 pointA = identity;
  pointA[12] = 1.0f;
  Mat4 pointB = identity;
  pointB[12] = 2.0f;
  Mat4 pointC = identity;
  pointC[12] = 3.0f;

  Light directional;
  directional.kind = LightKind::Directional;
  Light lightA;
  lightA.kind = LightKind::Point;
  lightA.intensity = 1.0f;
  Light lightB;
  lightB.kind = LightKind::Point;
  lightB.intensity = 2.0f;
  Light lightC;
  lightC.kind = LightKind::Point;
  lightC.intensity = 3.0f;

  const std::vector<LightExtractionInput> inputs{
      {lightA, pointA}, {directional, identity}, {lightB, pointB}, {lightC, pointC}};

  const auto result = extractFrameLightingData(inputs);
  REQUIRE(result.isOk());
  const FrameLightingData& data = result.value();
  REQUIRE(data.directionalLightCount == 1);
  REQUIRE(data.pointLightCount == 3);
  // Point lights appear in the order they were supplied (A, B, C), not
  // reordered relative to the interleaved Directional entry.
  REQUIRE(std::abs(data.pointLights[0].position[0] - 1.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[0].intensity - 1.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[1].position[0] - 2.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[1].intensity - 2.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[2].position[0] - 3.0f) < kEpsilon);
  REQUIRE(std::abs(data.pointLights[2].intensity - 3.0f) < kEpsilon);
}

TEST_CASE("extractFrameLightingData(): a near-zero-length Directional direction returns "
          "Err(DegenerateLightDirection)",
          "[runtime][scene_extraction][light]") {
  // Mirrors extractCameraMatrices()'s own "near-zero-length forward"
  // degenerate test verbatim -- same matrix shape, same epsilon boundary.
  const Mat4 degenerate{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1e-9f, 0, 0, 0, 0, 1};
  Light light;
  light.kind = LightKind::Directional;

  const auto result = extractFrameLightingData({LightExtractionInput{light, degenerate}});
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::DegenerateLightDirection);
}

TEST_CASE("extractFrameLightingData(): an empty lights vector returns a zero-count, value-initialized result",
          "[runtime][scene_extraction][light]") {
  const auto result = extractFrameLightingData({});
  REQUIRE(result.isOk());
  REQUIRE(result.value().directionalLightCount == 0);
  REQUIRE(result.value().pointLightCount == 0);
}

// Plan 0019 Section P8: TooManyLights' own third, non-Result path -- a
// lights vector violating either cap is a programmer error, fails fast
// via ATLANTIS_CHECK_MSG (never a Result::Err, silent truncation, or
// out-of-bounds write). Verified via the same replaceable
// atlantis::assertions::setFailureHandler() mechanism assert_tests.cpp
// already establishes as this codebase's own precedent for observing an
// ATLANTIS_CHECK_MSG failure without actually aborting the test process.
TEST_CASE("extractFrameLightingData(): a second Directional light fails fast via ATLANTIS_CHECK_MSG, never "
          "writing past directionalLights[0]",
          "[runtime][scene_extraction][light]") {
  int failureCount = 0;
  auto previous = atlantis::assertions::setFailureHandler([&failureCount](const atlantis::AssertFailureInfo&) {
    ++failureCount;
  });

  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Light first;
  first.kind = LightKind::Directional;
  first.intensity = 1.0f;
  Light second;
  second.kind = LightKind::Directional;
  second.intensity = 99.0f;

  const auto result = extractFrameLightingData({LightExtractionInput{first, identity}, LightExtractionInput{second, identity}});

  atlantis::assertions::setFailureHandler(std::move(previous));

  REQUIRE(failureCount == 1);
  REQUIRE(result.isOk());
  // The first, valid Directional light's own values are unmodified by
  // the rejected second entry.
  REQUIRE(result.value().directionalLightCount == 1);
  REQUIRE(std::abs(result.value().directionalLights[0].intensity - 1.0f) < kEpsilon);
}

TEST_CASE("extractFrameLightingData(): a fifth Point light fails fast via ATLANTIS_CHECK_MSG, never writing "
          "past pointLights[3]",
          "[runtime][scene_extraction][light]") {
  int failureCount = 0;
  auto previous = atlantis::assertions::setFailureHandler([&failureCount](const atlantis::AssertFailureInfo&) {
    ++failureCount;
  });

  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  std::vector<LightExtractionInput> inputs;
  for (int i = 0; i < 5; ++i) {
    Light point;
    point.kind = LightKind::Point;
    point.intensity = static_cast<float>(i) + 1.0f;
    inputs.push_back({point, identity});
  }

  const auto result = extractFrameLightingData(inputs);

  atlantis::assertions::setFailureHandler(std::move(previous));

  REQUIRE(failureCount == 1);
  REQUIRE(result.isOk());
  REQUIRE(result.value().pointLightCount == 4);
  REQUIRE(std::abs(result.value().pointLights[3].intensity - 4.0f) < kEpsilon);
}

// ---------------------------------------------------------------------
// checkConformalTransform() -- Plan 0019 Sections P8/P15 (Spec 0019 D7),
// Milestone 7/V12. Every hand-constructed 3x3 case D7's own proof
// sketch names.
// ---------------------------------------------------------------------

TEST_CASE("checkConformalTransform(): identity is accepted", "[runtime][scene_extraction][light]") {
  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  REQUIRE(checkConformalTransform(identity).isOk());
}

TEST_CASE("checkConformalTransform(): a pure rotation is accepted", "[runtime][scene_extraction][light]") {
  // A 90-degree rotation about Z: column0=(0,1,0), column1=(-1,0,0), column2=(0,0,1).
  const Mat4 rotation{0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  REQUIRE(checkConformalTransform(rotation).isOk());
}

TEST_CASE("checkConformalTransform(): a uniform positive scale is accepted", "[runtime][scene_extraction][light]") {
  const Mat4 uniformScale{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
  REQUIRE(checkConformalTransform(uniformScale).isOk());
}

TEST_CASE("checkConformalTransform(): a uniform NEGATIVE scale (a full point reflection) is accepted",
          "[runtime][scene_extraction][light]") {
  // D7's own explicitly-named "uniform scale of either sign" case -- -I is
  // itself an orthogonal matrix, so this must be accepted, not rejected.
  const Mat4 negativeUniformScale{-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
  REQUIRE(checkConformalTransform(negativeUniformScale).isOk());
}

TEST_CASE("checkConformalTransform(): non-uniform scale is rejected", "[runtime][scene_extraction][light]") {
  const Mat4 nonUniformScale{2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const auto result = checkConformalTransform(nonUniformScale);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::NonConformalNormalTransform);
}

TEST_CASE("checkConformalTransform(): shear is rejected", "[runtime][scene_extraction][light]") {
  // column1 = (1,1,0) -- a nonzero (0,1) entry breaking column0/column1
  // orthogonality beyond what any pure rotation would produce.
  const Mat4 shear{1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const auto result = checkConformalTransform(shear);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::NonConformalNormalTransform);
}

TEST_CASE("checkConformalTransform(): a degenerate (near-zero-length) column is rejected",
          "[runtime][scene_extraction][light]") {
  const Mat4 degenerate{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1e-9f, 0, 0, 0, 0, 1};
  const auto result = checkConformalTransform(degenerate);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == SceneExtractionError::NonConformalNormalTransform);
}

// ---------------------------------------------------------------------
// computeLambertianDiffuse() -- Plan 0019 Section P14. Every value below
// is hand-derived from D6's own written formula, never produced by
// calling extractFrameLightingData()/computeLambertianDiffuse() itself
// and asserting the output equals itself.
// ---------------------------------------------------------------------

TEST_CASE("computeLambertianDiffuse(): a single Directional light facing the surface contributes "
          "color * intensity * ndotl",
          "[runtime][scene_extraction][light]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[0] = 0.0f;
  lighting.directionalLights[0].direction[1] = 0.0f;
  lighting.directionalLights[0].direction[2] = -1.0f;  // light travels toward -z
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 0.0f;
  lighting.directionalLights[0].color[2] = 0.0f;
  lighting.directionalLights[0].intensity = 2.0f;

  // L = -direction = (0,0,1); N = (0,0,1) -- directly facing the light.
  // ndotl = dot(N, L) = 1. Expected: (1,0,0) * 2 * 1 = (2,0,0).
  const Vec3 result = computeLambertianDiffuse(Vec3{0, 0, 0}, Vec3{0, 0, 1}, lighting);
  REQUIRE(std::abs(result.x - 2.0f) < kEpsilon);
  REQUIRE(std::abs(result.y - 0.0f) < kEpsilon);
  REQUIRE(std::abs(result.z - 0.0f) < kEpsilon);
}

TEST_CASE("computeLambertianDiffuse(): a Directional light facing away from the surface is clamped to zero, "
          "not a negative contribution",
          "[runtime][scene_extraction][light]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[2] = -1.0f;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].intensity = 2.0f;

  // N = (0,0,-1) -- facing away from L = (0,0,1). ndotl = max(-1, 0) = 0.
  const Vec3 result = computeLambertianDiffuse(Vec3{0, 0, 0}, Vec3{0, 0, -1}, lighting);
  REQUIRE(std::abs(result.x - 0.0f) < kEpsilon);
  REQUIRE(std::abs(result.y - 0.0f) < kEpsilon);
  REQUIRE(std::abs(result.z - 0.0f) < kEpsilon);
}

TEST_CASE("computeLambertianDiffuse(): a single Point light contributes color * intensity * ndotl * "
          "linear-range-attenuation",
          "[runtime][scene_extraction][light]") {
  FrameLightingData lighting;
  lighting.pointLightCount = 1;
  lighting.pointLights[0].position[0] = 0.0f;
  lighting.pointLights[0].position[1] = 0.0f;
  lighting.pointLights[0].position[2] = 5.0f;
  lighting.pointLights[0].range = 10.0f;
  lighting.pointLights[0].color[1] = 1.0f;  // green
  lighting.pointLights[0].intensity = 3.0f;

  // worldPosition = (0,0,0), N = (0,0,1). toLight = (0,0,5), dist = 5,
  // L = (0,0,1), ndotl = 1. atten = clamp(1 - 5/10, 0, 1) = 0.5.
  // Expected: (0,1,0) * 3 * 1 * 0.5 = (0, 1.5, 0).
  const Vec3 result = computeLambertianDiffuse(Vec3{0, 0, 0}, Vec3{0, 0, 1}, lighting);
  REQUIRE(std::abs(result.x - 0.0f) < kEpsilon);
  REQUIRE(std::abs(result.y - 1.5f) < kEpsilon);
  REQUIRE(std::abs(result.z - 0.0f) < kEpsilon);
}

TEST_CASE("computeLambertianDiffuse(): a Point light beyond its own range attenuates to exactly zero",
          "[runtime][scene_extraction][light]") {
  FrameLightingData lighting;
  lighting.pointLightCount = 1;
  lighting.pointLights[0].position[2] = 15.0f;
  lighting.pointLights[0].range = 10.0f;  // dist (15) > range (10)
  lighting.pointLights[0].color[0] = 1.0f;
  lighting.pointLights[0].intensity = 1.0f;

  const Vec3 result = computeLambertianDiffuse(Vec3{0, 0, 0}, Vec3{0, 0, 1}, lighting);
  REQUIRE(std::abs(result.x - 0.0f) < kEpsilon);
}

TEST_CASE("computeLambertianDiffuse(): a Point light at (near-)zero distance is clamped by "
          "kPointLightDistanceEpsilon, never dividing by zero",
          "[runtime][scene_extraction][light]") {
  FrameLightingData lighting;
  lighting.pointLightCount = 1;
  // Actual distance (1e-6) is below kPointLightDistanceEpsilon (1e-4) --
  // dist clamps to 1e-4, so L = toLight / 1e-4 = (0.01, 0, 0).
  lighting.pointLights[0].position[0] = 1e-6f;
  lighting.pointLights[0].range = 1.0f;
  lighting.pointLights[0].color[0] = 1.0f;
  lighting.pointLights[0].color[1] = 1.0f;
  lighting.pointLights[0].color[2] = 1.0f;
  lighting.pointLights[0].intensity = 1.0f;

  const Vec3 result = computeLambertianDiffuse(Vec3{0, 0, 0}, Vec3{1, 0, 0}, lighting);
  // No NaN/Inf produced -- every component is finite.
  REQUIRE(std::isfinite(result.x));
  REQUIRE(std::isfinite(result.y));
  REQUIRE(std::isfinite(result.z));
  // ndotl = dot((1,0,0), (0.01,0,0)) = 0.01; atten = clamp(1 - 1e-4/1, 0, 1)
  // ~= 0.9999. Expected ~= 1 * 1 * 0.01 * 0.9999 ~= 0.009999.
  REQUIRE(std::abs(result.x - 0.009999f) < 1e-3f);
}

TEST_CASE("computeLambertianDiffuse(): zero lights of either kind returns exactly the zero vector, no ambient term",
          "[runtime][scene_extraction][light]") {
  const FrameLightingData lighting{};
  const Vec3 result = computeLambertianDiffuse(Vec3{0, 0, 0}, Vec3{0, 0, 1}, lighting);
  REQUIRE(result.x == 0.0f);
  REQUIRE(result.y == 0.0f);
  REQUIRE(result.z == 0.0f);
}

// ---------------------------------------------------------------------
// FrameLightingData -- Plan 0019 Section P7 requirement 6: a dedicated,
// fixed-byte unit test with distinct, individually recognizable values
// in every field, never produced by calling a production serializer.
// ---------------------------------------------------------------------

namespace {

[[nodiscard]] std::uint32_t readU32(const std::byte* bytes, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

[[nodiscard]] float readFloat(const std::byte* bytes, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

[[nodiscard]] bool isZeroRange(const std::byte* bytes, std::size_t offset, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    if (bytes[offset + i] != std::byte{0}) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("FrameLightingData: a populated instance's own byte layout matches Plan 0019 Section P7's table "
          "exactly, and every unpopulated/padding byte reads back as zero",
          "[runtime][scene_extraction][light]") {
  FrameLightingData data{};
  data.directionalLightCount = 1;
  data.pointLightCount = 2;
  data.directionalLights[0].direction[0] = 0.1f;
  data.directionalLights[0].direction[1] = 0.2f;
  data.directionalLights[0].direction[2] = 0.3f;
  data.directionalLights[0].color[0] = 0.4f;
  data.directionalLights[0].color[1] = 0.5f;
  data.directionalLights[0].color[2] = 0.6f;
  data.directionalLights[0].intensity = 0.7f;
  data.pointLights[0].position[0] = 1.1f;
  data.pointLights[0].position[1] = 1.2f;
  data.pointLights[0].position[2] = 1.3f;
  data.pointLights[0].range = 1.4f;
  data.pointLights[0].color[0] = 1.5f;
  data.pointLights[0].color[1] = 1.6f;
  data.pointLights[0].color[2] = 1.7f;
  data.pointLights[0].intensity = 1.8f;
  data.pointLights[1].position[0] = 2.1f;
  data.pointLights[1].position[1] = 2.2f;
  data.pointLights[1].position[2] = 2.3f;
  data.pointLights[1].range = 2.4f;
  data.pointLights[1].color[0] = 2.5f;
  data.pointLights[1].color[1] = 2.6f;
  data.pointLights[1].color[2] = 2.7f;
  data.pointLights[1].intensity = 2.8f;
  // pointLights[2]/[3] deliberately left at their value-initialized zero.

  static_assert(sizeof(FrameLightingData) == 176);
  std::array<std::byte, 176> bytes{};
  std::memcpy(bytes.data(), &data, sizeof(data));

  REQUIRE(readU32(bytes.data(), 0) == 1);   // directionalLightCount
  REQUIRE(readU32(bytes.data(), 4) == 2);   // pointLightCount
  REQUIRE(isZeroRange(bytes.data(), 8, 8));  // _pad1[2]

  REQUIRE(std::abs(readFloat(bytes.data(), 16) - 0.1f) < kEpsilon);   // direction.x
  REQUIRE(std::abs(readFloat(bytes.data(), 20) - 0.2f) < kEpsilon);   // direction.y
  REQUIRE(std::abs(readFloat(bytes.data(), 24) - 0.3f) < kEpsilon);   // direction.z
  REQUIRE(isZeroRange(bytes.data(), 28, 4));                          // _pad0
  REQUIRE(std::abs(readFloat(bytes.data(), 32) - 0.4f) < kEpsilon);   // color.r
  REQUIRE(std::abs(readFloat(bytes.data(), 36) - 0.5f) < kEpsilon);   // color.g
  REQUIRE(std::abs(readFloat(bytes.data(), 40) - 0.6f) < kEpsilon);   // color.b
  REQUIRE(std::abs(readFloat(bytes.data(), 44) - 0.7f) < kEpsilon);   // intensity

  REQUIRE(std::abs(readFloat(bytes.data(), 48) - 1.1f) < kEpsilon);   // pointLights[0].position.x
  REQUIRE(std::abs(readFloat(bytes.data(), 52) - 1.2f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 56) - 1.3f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 60) - 1.4f) < kEpsilon);   // range
  REQUIRE(std::abs(readFloat(bytes.data(), 64) - 1.5f) < kEpsilon);   // color
  REQUIRE(std::abs(readFloat(bytes.data(), 68) - 1.6f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 72) - 1.7f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 76) - 1.8f) < kEpsilon);   // intensity

  REQUIRE(std::abs(readFloat(bytes.data(), 80) - 2.1f) < kEpsilon);   // pointLights[1].position.x
  REQUIRE(std::abs(readFloat(bytes.data(), 84) - 2.2f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 88) - 2.3f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 92) - 2.4f) < kEpsilon);   // range
  REQUIRE(std::abs(readFloat(bytes.data(), 96) - 2.5f) < kEpsilon);   // color
  REQUIRE(std::abs(readFloat(bytes.data(), 100) - 2.6f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 104) - 2.7f) < kEpsilon);
  REQUIRE(std::abs(readFloat(bytes.data(), 108) - 2.8f) < kEpsilon);  // intensity

  // pointLights[2]/[3] -- 64 bytes, 112-175 -- deliberately never
  // populated above, proving value-initialization actually zeroed them,
  // not merely that the struct compiles.
  REQUIRE(isZeroRange(bytes.data(), 112, 64));
}

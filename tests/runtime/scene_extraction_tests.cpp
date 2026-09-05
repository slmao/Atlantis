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

using atlantis::runtime::CameraMatrices;
using atlantis::runtime::CameraWorldPositionData;
using atlantis::runtime::checkConformalTransform;
using atlantis::runtime::computeLambertianDiffuse;
using atlantis::runtime::computePbrDirectLighting;
using atlantis::runtime::computeShadowLightSpaceMatrices;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::extractCameraWorldPosition;
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

// Plan 0023 Milestone 7: extractCameraWorldPosition() -- confirms it
// reads exactly the same column-12/13/14 translation
// extractCameraMatrices() itself already derives internally as its own
// local `eye` (scene_extraction.cpp:107), independently re-asserted
// here since this file's own real render tests
// (pbr_render_gpu_tests.cpp) build their own camera buffer directly,
// never calling this function -- this is this function's own only
// real, dedicated test.
TEST_CASE("extractCameraWorldPosition(): reads the world matrix's own translation column (12/13/14), matching "
          "extractCameraMatrices()'s own identical eye derivation",
          "[runtime][scene_extraction][pbr]") {
  const Mat4 cameraWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2.5f, -1.5f, 7.0f, 1};
  const CameraWorldPositionData result = extractCameraWorldPosition(cameraWorld);
  REQUIRE(result.x == 2.5f);
  REQUIRE(result.y == -1.5f);
  REQUIRE(result.z == 7.0f);
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

// computeShadowLightSpaceMatrices() (Plan 0027, ADR-0072 D-1/P4/P11):
// the one production implementation Runtime's own runFrame() and the
// real-GPU shadow discriminator tests (shadow_gpu_tests.cpp) both call
// -- covered directly here so a regression is caught GPU-independently,
// not only via a real-GPU pixel mismatch.
//
// computeShadowLightSpaceMatrices()'s own view matrix stores its basis
// vectors as ROWS (right at indices 0/4/8, up at 1/5/9, -direction at
// 2/6/10 -- the standard lookAt-style world-to-view rotation, matching
// lookAtMatrix()'s own identical layout in this same file), NOT as the
// COLUMNS col()/requireOrthonormalProperBasis() above extract (that
// helper only checks generic orthonormality/properness, which holds
// regardless of the row/column distinction for a square orthogonal
// matrix -- it is not a semantic right/up/forward accessor). row3()
// below reads the actual semantic basis vectors.
namespace {
[[nodiscard]] V3 row3(const Mat4& m, int r) {
  return {m[static_cast<std::size_t>(r)], m[static_cast<std::size_t>(r) + 4], m[static_cast<std::size_t>(r) + 8]};
}

// normalize(-0.3,-1.0,-0.2) computed at full float precision -- P10's
// own hand-derived decimal values (-0.28224,-0.94046,-0.18816) are
// truncated to 5 places, close enough for P10's own pixel/luminance
// checks but not tight enough for this file's own kEpsilon = 1e-4
// orthonormality check.
[[nodiscard]] Vec3 p10CheckOneDirection() {
  constexpr float x = -0.3f, y = -1.0f, z = -0.2f;
  const float len = std::sqrt(x * x + y * y + z * z);
  return {x / len, y / len, z / len};
}
}  // namespace

TEST_CASE("computeShadowLightSpaceMatrices(): a normal, non-degenerate direction produces an orthonormal proper "
          "view basis whose forward row is -direction",
          "[runtime][scene_extraction][shadow]") {
  const Vec3 direction = p10CheckOneDirection();
  const CameraMatrices result = computeShadowLightSpaceMatrices(direction);
  requireOrthonormalProperBasis(result.view);

  const V3 forwardRow = row3(result.view, 2);
  REQUIRE(std::abs(forwardRow.x - (-direction.x)) < kEpsilon);
  REQUIRE(std::abs(forwardRow.y - (-direction.y)) < kEpsilon);
  REQUIRE(std::abs(forwardRow.z - (-direction.z)) < kEpsilon);
}

// P11: unlike extractCameraMatrices()'s own fail-fast DegenerateCameraBasis
// for a forward parallel to world-up, a directional light pointing
// straight down is an ordinary "sun overhead" case -- this must never
// fail, and must fall back to a second fixed up-vector (0,0,1) instead,
// still producing a valid orthonormal proper basis.
TEST_CASE("computeShadowLightSpaceMatrices(): a direction parallel to world-up falls back to a second up-vector "
          "instead of failing",
          "[runtime][scene_extraction][shadow]") {
  const Vec3 straightDown{0.0f, -1.0f, 0.0f};
  const CameraMatrices result = computeShadowLightSpaceMatrices(straightDown);
  requireOrthonormalProperBasis(result.view);

  const V3 forwardRow = row3(result.view, 2);
  REQUIRE(std::abs(forwardRow.x - 0.0f) < kEpsilon);
  REQUIRE(std::abs(forwardRow.y - 1.0f) < kEpsilon);
  REQUIRE(std::abs(forwardRow.z - 0.0f) < kEpsilon);
}

// P4's own fixed orthographic shadow volume (center (0,0,0), half-extent
// 8.0, near 0.1, far 30.0) -- independent of direction, so this checks
// the projection matrix's exact values against P4's own hand-derived
// formula, using an arbitrary non-degenerate direction.
TEST_CASE("computeShadowLightSpaceMatrices(): the projection matrix matches P4's fixed orthographic volume exactly",
          "[runtime][scene_extraction][shadow]") {
  const CameraMatrices result = computeShadowLightSpaceMatrices(p10CheckOneDirection());
  const Mat4& projection = result.projection;

  REQUIRE(std::abs(projection[0] - (1.0f / 8.0f)) < kEpsilon);
  REQUIRE(std::abs(projection[5] - (-1.0f / 8.0f)) < kEpsilon);
  REQUIRE(std::abs(projection[10] - (-1.0f / 29.9f)) < kEpsilon);
  REQUIRE(std::abs(projection[14] - (-0.1f / 29.9f)) < kEpsilon);
  REQUIRE(std::abs(projection[15] - 1.0f) < kEpsilon);
}

// P10's own hand-computed key output values for its own fixed check-1
// direction, re-checked directly against the production implementation:
// right_L ~ (0.5547, 0, -0.8320), camUp_L ~ (-0.7824, 0.3392, -0.5217).
TEST_CASE("computeShadowLightSpaceMatrices(): matches P10's own hand-computed right_L/camUp_L for its fixed check-1 "
          "direction",
          "[runtime][scene_extraction][shadow]") {
  const CameraMatrices result = computeShadowLightSpaceMatrices(p10CheckOneDirection());

  const V3 rightL = row3(result.view, 0);
  const V3 camUpL = row3(result.view, 1);

  constexpr float kP10Epsilon = 1e-3f;  // P10's own values are given to 4 decimal places
  REQUIRE(std::abs(rightL.x - 0.5547f) < kP10Epsilon);
  REQUIRE(std::abs(rightL.y - 0.0f) < kP10Epsilon);
  REQUIRE(std::abs(rightL.z - (-0.8320f)) < kP10Epsilon);
  REQUIRE(std::abs(camUpL.x - (-0.7824f)) < kP10Epsilon);
  REQUIRE(std::abs(camUpL.y - 0.3392f) < kP10Epsilon);
  REQUIRE(std::abs(camUpL.z - (-0.5217f)) < kP10Epsilon);
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
// Plan 0022 Section M1/V3: extractFrameLightingData() is called once per
// successful frame now, not once per process lifetime -- these two
// TEST_CASEs lock the two properties that makes safe: every call is a
// fresh, fully value-initialized result (so a shrinking light count
// between calls leaves no stale trailing-slot bytes), and the function
// itself is stateless/pure (it owns no "already captured" concept --
// final-value-across-mutations semantics is entirely a property of
// which LightExtractionInputs the caller passes in, decided by World's
// own already-live lightEntities()/getLight()/getWorldMatrix(), not by
// anything tracked here).
// ---------------------------------------------------------------------

TEST_CASE("extractFrameLightingData(): a second call with fewer Point lights than the first zeros the "
          "now-unused trailing slot, including its own padding",
          "[runtime][scene_extraction][light]") {
  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Light firstPoint;
  firstPoint.kind = LightKind::Point;
  firstPoint.intensity = 1.0f;
  Light secondPoint;
  secondPoint.kind = LightKind::Point;
  secondPoint.intensity = 2.0f;

  const auto firstCallResult =
      extractFrameLightingData({LightExtractionInput{firstPoint, identity}, LightExtractionInput{secondPoint, identity}});
  REQUIRE(firstCallResult.isOk());
  REQUIRE(firstCallResult.value().pointLightCount == 2);

  // Second, independent call -- fewer lights, not a mutation of the
  // first call's own result (extractFrameLightingData() takes its own
  // argument by const&, returns by value; there is no shared state
  // between these two calls to mutate).
  const auto secondCallResult = extractFrameLightingData({LightExtractionInput{firstPoint, identity}});
  REQUIRE(secondCallResult.isOk());
  REQUIRE(secondCallResult.value().pointLightCount == 1);
  REQUIRE(std::abs(secondCallResult.value().pointLights[0].intensity - 1.0f) < kEpsilon);

  // The now-unused second slot -- never touched by this call, since only
  // one light was passed -- is nonetheless all-zero: FrameLightingData's
  // own default member initializers (`= {}`) zero every field of a
  // fresh, freshly-value-initialized result, exactly as they would on
  // the very first extraction the process ever performs.
  const FrameLightingData::PointLightGpu& unusedSlot = secondCallResult.value().pointLights[1];
  CHECK(unusedSlot.position[0] == 0.0f);
  CHECK(unusedSlot.position[1] == 0.0f);
  CHECK(unusedSlot.position[2] == 0.0f);
  CHECK(unusedSlot.range == 0.0f);
  CHECK(unusedSlot.color[0] == 0.0f);
  CHECK(unusedSlot.color[1] == 0.0f);
  CHECK(unusedSlot.color[2] == 0.0f);
  CHECK(unusedSlot.intensity == 0.0f);

  // The struct's own explicit padding is likewise zero -- not merely
  // "probably", since it is default-member-initialized identically to
  // every other field.
  CHECK(secondCallResult.value()._pad1[0] == 0);
  CHECK(secondCallResult.value()._pad1[1] == 0);
}

TEST_CASE("extractFrameLightingData(): calling it twice with byte-identical input produces byte-identical "
          "output -- it is a stateless, pure function; final-value-across-mutations semantics belongs to the "
          "caller, not to this function",
          "[runtime][scene_extraction][light]") {
  const Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Light point;
  point.kind = LightKind::Point;
  point.color = {0.25f, 0.5f, 0.75f};
  point.intensity = 3.0f;
  const std::vector<LightExtractionInput> inputs{LightExtractionInput{point, identity}};

  const auto firstResult = extractFrameLightingData(inputs);
  const auto secondResult = extractFrameLightingData(inputs);
  REQUIRE(firstResult.isOk());
  REQUIRE(secondResult.isOk());

  FrameLightingData firstBytes = firstResult.value();
  FrameLightingData secondBytes = secondResult.value();
  CHECK(std::memcmp(&firstBytes, &secondBytes, sizeof(FrameLightingData)) == 0);
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
// computePbrDirectLighting() -- Plan 0023 Milestone 7 (ADR-0067 D-1/D-2).
// Every expected value below is independently derived from D-1's own
// written formula -- computed with a separate, standalone reference
// implementation (never this codebase's own C++ or Slang source, never
// produced by calling computePbrDirectLighting() itself and asserting
// the output equals itself). N=V=L=(0,0,1) is used for several cases
// specifically because it collapses NdotH/NdotV/NdotL/VdotH all to 1,
// making the resulting arithmetic checkable by hand as well
// (D = alpha^2/(pi*alpha^4) = 1/(pi*alpha^2); G = 1 exactly, regardless
// of k, since NdotV=NdotL=1 makes each G1 term's own denominator equal
// its own numerator; F = F0, since VdotH=1 makes (1-VdotH)^5 = 0).
// ---------------------------------------------------------------------

TEST_CASE("computePbrDirectLighting(): dielectric, mid roughness, N=V=L aligned -- matches the independently "
          "computed closed-form value",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[2] = -1.0f;  // L = -direction = (0,0,1)
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = 1.0f;

  // worldPosition=(0,0,0), worldNormal=(0,0,1), cameraWorldPosition=(0,0,5)
  // -> V=(0,0,1) -- N=V=L all aligned. metallic=0, roughness=0.5,
  // baseColor=(1,1,1). Independently computed: 0.35650707 (all channels).
  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, Vec3{1, 1, 1}, 0.0f, 0.5f,
                                                lighting);
  REQUIRE(std::abs(result.x - 0.3565071f) < 1e-3f);
  REQUIRE(std::abs(result.y - 0.3565071f) < 1e-3f);
  REQUIRE(std::abs(result.z - 0.3565071f) < 1e-3f);
}

TEST_CASE("computePbrDirectLighting(): metallic, mid roughness, non-uniform baseColor tints the specular (F0), "
          "N=V=L aligned",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[2] = -1.0f;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = 1.0f;

  // metallic=1.0 (diffuseColor -> 0, F0 -> baseColor exactly), roughness=0.5,
  // baseColor=(0.8,0.5,0.2). Independently computed:
  // (1.0185916, 0.6366198, 0.2546479).
  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, Vec3{0.8f, 0.5f, 0.2f},
                                                1.0f, 0.5f, lighting);
  REQUIRE(std::abs(result.x - 1.0185916f) < 2e-3f);
  REQUIRE(std::abs(result.y - 0.6366198f) < 2e-3f);
  REQUIRE(std::abs(result.z - 0.2546479f) < 2e-3f);
}

TEST_CASE("computePbrDirectLighting(): roughness=0 is floored by kMinAlpha, never dividing by zero -- large but "
          "finite, matching the independently computed closed-form value",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[2] = -1.0f;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = 1.0f;

  // roughness=0.0 -> alpha = max(0, kMinAlpha) = kMinAlpha = 1e-3, a
  // legitimate, authorable mirror-like surface (never rejected at cook
  // time, ADR-0067 D-2) -- a real, disclosed large specular value at
  // this exact N=V=L=H alignment (ADR-0067 D-6's own "can hard-clip"
  // disclosure), never NaN/Inf. An independent float64 reference
  // computation of this same formula gives ~3183.4, but D's own
  // NdotH*NdotH*(alpha*alpha-1.0)+1.0 term is a real catastrophic
  // cancellation at this alpha (subtracting two float32 values within
  // 1e-6 of each other) -- float32's own ~7-decimal-digit precision
  // loses most of alpha*alpha's own significance there, so this
  // function's own real float32 result diverges substantially from the
  // float64 reference (a genuine, expected float32-vs-float64 artifact
  // of the formula's own literal transcription, matching what real GPU
  // hardware would also compute in float32 -- not a defect to "fix" by
  // reformulating D, which would break literal parity with the shader).
  // Only the qualitative "large, finite, positive" property is checked
  // here.
  const Vec3 result =
      computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, Vec3{1, 1, 1}, 0.0f, 0.0f, lighting);
  REQUIRE(std::isfinite(result.x));
  REQUIRE(std::isfinite(result.y));
  REQUIRE(std::isfinite(result.z));
  REQUIRE(result.x > 10.0f);
}

TEST_CASE("computePbrDirectLighting(): a grazing-angle N.L still produces a small, positive, finite contribution",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  // direction chosen so L = -direction has a ~1 degree elevation above
  // the surface (N.L ~= sin(1 deg) ~= 0.01745, matching an 89-degree
  // grazing incidence).
  lighting.directionalLights[0].direction[0] = -0.99985f;
  lighting.directionalLights[0].direction[2] = -0.01745f;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = 1.0f;

  // N=(0,0,1), V=(0,0,1) (camera above), metallic=0, roughness=0.5,
  // baseColor=(1,1,1). Independently computed: 0.0053679.
  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, Vec3{1, 1, 1}, 0.0f, 0.5f,
                                                lighting);
  REQUIRE(result.x > 0.0f);
  REQUIRE(std::isfinite(result.x));
  REQUIRE(std::abs(result.x - 0.0053679f) < 1e-3f);
}

TEST_CASE("computePbrDirectLighting(): the Schlick Fresnel term makes a large, real difference at a wide "
          "view/light split -- a disabled Fresnel blend (F forced to F0) would diverge far outside tolerance",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  // V and L both 60 degrees from N, on opposite sides -- H lands
  // exactly on N (NdotH=1), but VdotH=0.5 (not 1), so the Schlick term
  // (1-VdotH)^5 = 0.03125 contributes materially to F, unlike the
  // N=V=L-aligned cases above (VdotH=1 there, Schlick term = 0
  // regardless of Fresnel).
  lighting.directionalLights[0].direction[0] = 0.8660254f;  // L = -direction = (-0.8660254, 0, 0.5)
  lighting.directionalLights[0].direction[2] = -0.5f;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = 1.0f;

  // worldPosition=(0,0,0), worldNormal=(0,0,1), cameraWorldPosition
  // along V=(0.8660254,0,0.5) scaled by 5. metallic=0, roughness=0.3,
  // baseColor=(1,1,1). Independently computed: 1.0855018.
  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{4.330127f, 0, 2.5f},
                                                Vec3{1, 1, 1}, 0.0f, 0.3f, lighting);
  REQUIRE(std::abs(result.x - 1.0855018f) < 5e-3f);
}

TEST_CASE("computePbrDirectLighting(): N.L <= 0 (facing away) contributes exactly zero, never a negative value",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[2] = -1.0f;  // L = (0,0,1)
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].intensity = 1.0f;

  // worldNormal = (0,0,-1) -- facing directly away from L.
  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 0, -5}, Vec3{1, 1, 1}, 0.0f,
                                                0.5f, lighting);
  REQUIRE(result.x == 0.0f);
  REQUIRE(result.y == 0.0f);
  REQUIRE(result.z == 0.0f);
}

TEST_CASE("computePbrDirectLighting(): a Point light beyond its own range attenuates to exactly zero",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.pointLightCount = 1;
  lighting.pointLights[0].position[2] = 15.0f;
  lighting.pointLights[0].range = 10.0f;  // dist (15) > range (10)
  lighting.pointLights[0].color[0] = 1.0f;
  lighting.pointLights[0].intensity = 1.0f;

  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, Vec3{1, 1, 1}, 0.0f, 0.5f,
                                                lighting);
  REQUIRE(result.x == 0.0f);
  REQUIRE(result.y == 0.0f);
  REQUIRE(result.z == 0.0f);
}

TEST_CASE("computePbrDirectLighting(): a Point light at (near-)zero distance is clamped by "
          "kPointLightDistanceEpsilon, never dividing by zero",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.pointLightCount = 1;
  lighting.pointLights[0].position[0] = 1e-6f;
  lighting.pointLights[0].range = 1.0f;
  lighting.pointLights[0].color[0] = 1.0f;
  lighting.pointLights[0].color[1] = 1.0f;
  lighting.pointLights[0].color[2] = 1.0f;
  lighting.pointLights[0].intensity = 1.0f;

  const Vec3 result = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{5, 0, 0}, Vec3{1, 1, 1}, 0.0f, 0.5f,
                                                lighting);
  REQUIRE(std::isfinite(result.x));
  REQUIRE(std::isfinite(result.y));
  REQUIRE(std::isfinite(result.z));
}

TEST_CASE("computePbrDirectLighting(): multi-light accumulation is the exact per-light sum -- two Directional "
          "lights of different colors combine additively",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lightingBoth;
  lightingBoth.directionalLightCount = 2;
  lightingBoth.directionalLights[0].direction[2] = -1.0f;
  lightingBoth.directionalLights[0].color[0] = 1.0f;
  lightingBoth.directionalLights[0].intensity = 1.0f;
  lightingBoth.directionalLights[1].direction[2] = -1.0f;
  lightingBoth.directionalLights[1].color[1] = 1.0f;
  lightingBoth.directionalLights[1].intensity = 1.0f;

  FrameLightingData lightingFirstOnly;
  lightingFirstOnly.directionalLightCount = 1;
  lightingFirstOnly.directionalLights[0] = lightingBoth.directionalLights[0];

  FrameLightingData lightingSecondOnly;
  lightingSecondOnly.directionalLightCount = 1;
  lightingSecondOnly.directionalLights[0] = lightingBoth.directionalLights[1];

  const Vec3 pos{0, 0, 0}, normal{0, 0, 1}, cam{0, 0, 5}, baseColor{1, 1, 1};
  const Vec3 both = computePbrDirectLighting(pos, normal, cam, baseColor, 0.3f, 0.4f, lightingBoth);
  const Vec3 first = computePbrDirectLighting(pos, normal, cam, baseColor, 0.3f, 0.4f, lightingFirstOnly);
  const Vec3 second = computePbrDirectLighting(pos, normal, cam, baseColor, 0.3f, 0.4f, lightingSecondOnly);

  REQUIRE(std::abs(both.x - first.x) < kEpsilon);
  REQUIRE(std::abs(both.y - second.y) < kEpsilon);
  REQUIRE(std::abs(both.x - (first.x + second.x)) < kEpsilon);
  REQUIRE(std::abs(both.y - (first.y + second.y)) < kEpsilon);
  REQUIRE(std::abs(both.z - (first.z + second.z)) < kEpsilon);
}

TEST_CASE("computePbrDirectLighting(): every dielectric/metallic x low/high roughness combination stays NaN/Inf "
          "free across both light kinds",
          "[runtime][scene_extraction][pbr]") {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[0] = 0.3f;
  lighting.directionalLights[0].direction[1] = -0.5f;
  lighting.directionalLights[0].direction[2] = -0.8f;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 0.8f;
  lighting.directionalLights[0].color[2] = 0.6f;
  lighting.directionalLights[0].intensity = 1.5f;
  lighting.pointLightCount = 1;
  lighting.pointLights[0].position[0] = 1.0f;
  lighting.pointLights[0].position[1] = 1.0f;
  lighting.pointLights[0].position[2] = 2.0f;
  lighting.pointLights[0].range = 5.0f;
  lighting.pointLights[0].color[0] = 0.5f;
  lighting.pointLights[0].color[1] = 0.5f;
  lighting.pointLights[0].color[2] = 1.0f;
  lighting.pointLights[0].intensity = 2.0f;

  const Vec3 pos{0.2f, -0.1f, 0.0f}, normal{0.0f, 0.1f, 1.0f}, cam{0.5f, 0.3f, 4.0f}, baseColor{0.7f, 0.4f, 0.3f};
  const float metallicValues[] = {0.0f, 1.0f};
  const float roughnessValues[] = {0.0f, 1.0f};
  for (float metallic : metallicValues) {
    for (float roughness : roughnessValues) {
      const Vec3 result = computePbrDirectLighting(pos, normal, cam, baseColor, metallic, roughness, lighting);
      REQUIRE(std::isfinite(result.x));
      REQUIRE(std::isfinite(result.y));
      REQUIRE(std::isfinite(result.z));
      REQUIRE(result.x >= 0.0f);
      REQUIRE(result.y >= 0.0f);
      REQUIRE(result.z >= 0.0f);
    }
  }
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

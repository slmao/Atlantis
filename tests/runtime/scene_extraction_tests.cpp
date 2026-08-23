#include <atlantis/runtime/scene_extraction.h>

#include <cmath>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::Mat4;
using atlantis::runtime::resolveMeshAsset;
using atlantis::runtime::SceneExtractionError;

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

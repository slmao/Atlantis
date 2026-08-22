#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>

#include <array>
#include <variant>

namespace atlantis::runtime {

using Mat4 = std::array<float, 16>;

// See specs/0014-world-scene-foundation.md,
// adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md.
// Runtime-private: atlantis::runtime remains Runtime's own internal
// namespace, nothing here is consumed by any other module. Factored out
// of runtime_application.cpp's own anonymous namespace so this pure,
// GPU-independent logic is unit-testable (no Device, no GPU, no World
// instance required).
enum class SceneExtractionError {
  NoActiveCamera,
  DegenerateCameraForward,  // column 2's own world-space image has near-zero length
  DegenerateCameraBasis,    // forward is (near-)parallel to the canonical world-up axis
  UnresolvedMeshAsset,      // a Renderable's AssetId matches no known, resolved asset
};

struct CameraMatrices {
  Mat4 view;
  Mat4 projection;
};

// Moved out of runtime_application.cpp's own anonymous namespace (where
// an identically-shaped copy previously lived) so tests/runtime/ can
// call them directly -- duplicated, not shared, with the separate copies
// already in examples/minimal_renderer_demo and
// tests/image_regression/fixture/minimal_cube_fixture.cpp, matching this
// codebase's own established "duplicated, not shared" precedent for this
// exact helper set.
[[nodiscard]] Mat4 identityMatrix();
[[nodiscard]] Mat4 lookAtMatrix(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ);
[[nodiscard]] Mat4 perspectiveMatrix(float fovYRadians, float aspect, float nearZ, float farZ);

// Extracts eye+forward ONLY from cameraWorldMatrix (never a right/up
// column -- ADR-0051's own Decision step 3, since those are not
// reliably orthogonal under a sheared hierarchy), feeds them into the
// same lookAt()-shaped construction every existing composition root
// already uses, and builds the projection matrix from
// fovYRadians/nearZ/farZ and the caller-supplied aspect ratio. Detects
// both degenerate-input cases explicitly before ever calling
// lookAtMatrix(), so that function is never invoked with an input that
// would make it divide by a near-zero length.
[[nodiscard]] atlantis::Result<CameraMatrices, SceneExtractionError> extractCameraMatrices(
    const Mat4& cameraWorldMatrix, float fovYRadians, float nearZ, float farZ, float aspect);

// Trivial by design (ADR-0051's own Decision step 4 fixes only the
// existence and input/output shape of asset resolution, not a
// container) -- this Plan's own validation scene resolves against
// exactly one known AssetId; Ok() on a match, Err(UnresolvedMeshAsset)
// otherwise.
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> resolveMeshAsset(
    atlantis::asset_system::AssetId requested, atlantis::asset_system::AssetId known);

}  // namespace atlantis::runtime

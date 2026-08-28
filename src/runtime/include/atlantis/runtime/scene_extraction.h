#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>

#include <array>
#include <variant>
#include <vector>

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
  // Plan 0018 Section P9: a sibling enumerator, not a new family -- the
  // identical per-entity, per-frame resolution-failure class
  // UnresolvedMeshAsset already names, applied to a material AssetId
  // instead of a mesh one.
  UnresolvedMaterialAsset,
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
// container). Plan 0015 Section D10: knownIds is the currently-
// resolved/loaded AssetId set (RuntimeApplication's own
// meshResourceMap_ keys, collected by the caller) -- Ok() if requested
// is a member, Err(UnresolvedMeshAsset) otherwise. Takes a plain
// std::vector rather than the map itself so this Runtime-private
// header stays decoupled from atlantis::renderer::Mesh's own complete
// type; the caller still uses meshResourceMap_.at(requested) directly
// to obtain the actual Mesh once this call confirms membership.
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> resolveMeshAsset(
    atlantis::asset_system::AssetId requested, const std::vector<atlantis::asset_system::AssetId>& knownIds);

// Plan 0018 Section P9: verbatim-shaped after resolveMeshAsset() -- same
// linear std::find, same decoupled-from-the-concrete-resource-type
// rationale. knownIds is the caller's own currently-realized material
// AssetId set (materialResourceMap_'s own keys, plus this frame's own
// newly-realized candidates, collected by the caller).
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> resolveMaterialAsset(
    atlantis::asset_system::AssetId requested, const std::vector<atlantis::asset_system::AssetId>& knownIds);

}  // namespace atlantis::runtime

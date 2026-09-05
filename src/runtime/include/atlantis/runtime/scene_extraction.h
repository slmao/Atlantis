#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>
#include <atlantis/world/light.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

namespace atlantis::runtime {

using Mat4 = std::array<float, 16>;

// Plan 0019 Section P14: promoted from an anonymous-namespace-local
// helper (scene_extraction.cpp's own pre-existing Vec3, used internally
// by extractCameraMatrices()) to this public header, since
// computeLambertianDiffuse() (P14, below) needs a public, nameable
// Vec3 parameter/return type. scene_extraction.cpp's own internal
// helpers (length(), cross()) now operate on this single, public
// definition instead of a separate, file-local one -- one Vec3 type in
// this file, not two.
struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

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
  // Plan 0019 Section P8: mirrors DegenerateCameraForward's own
  // near-zero-length check, applied to a Directional Light's own
  // world-matrix-derived direction instead of a camera's own forward
  // vector.
  DegenerateLightDirection,
  // Plan 0019 Section P8/P15: a LitTextured-bound entity's own world
  // matrix fails checkConformalTransform() -- its upper-left 3x3 is not
  // (uniform-scale-times-)orthogonal, so the vertex shader's own direct
  // objectToWorld-based normal transform (P11) would be wrong.
  NonConformalNormalTransform,
};

struct CameraMatrices {
  Mat4 view;
  Mat4 projection;
};

// Plan 0019 Section P7: the single authoritative field table's own
// direct C++ transcription -- see plans/0019-lighting-foundation.md P7
// for the full, offset-by-offset rationale. Appended immediately after
// the existing 128-byte camera view+projection block inside the same
// uniform Buffer (runtime_application.cpp / P9) -- this struct's own
// sizeof() is the exact byte count appended, no gap, no overlap.
// Milestone 6's own real Slang reflection JSON cross-check (P7
// requirement 7) confirmed this table matches the compiled
// lit_textured.slang CameraUniform layout exactly (offsets 128/132/136/
// 144/176 in the absolute buffer, i.e. 0/4/8/16/48 relative to this
// struct's own start).
struct alignas(16) FrameLightingData {
  std::uint32_t directionalLightCount = 0;  // offset 0
  std::uint32_t pointLightCount = 0;        // offset 4
  std::uint32_t _pad1[2] = {};              // offset 8 -- explicit padding,
                                             // never relied on as an implicit
                                             // compiler-inserted gap (std140's
                                             // own vec3-array alignment rule;
                                             // float[3] alone only demands
                                             // 4-byte C++ alignment, so this
                                             // gap would NOT appear without
                                             // this explicit field)
  struct alignas(16) DirectionalLightGpu {
    float direction[3] = {};  // offset 0 (within this 32-byte element)
    float _pad0 = 0.0f;       // offset 12 -- explicit, not implicit
    float color[3] = {};      // offset 16
    float intensity = 0.0f;   // offset 28
  } directionalLights[1]{};   // offset 16, 32 bytes total, array stride 32
  struct alignas(16) PointLightGpu {
    float position[3] = {};   // offset 0
    float range = 0.0f;       // offset 12
    float color[3] = {};      // offset 16
    float intensity = 0.0f;   // offset 28
  } pointLights[4]{};  // offset 48, 128 bytes total, array stride 32
};
static_assert(std::is_standard_layout_v<FrameLightingData>);
static_assert(std::is_standard_layout_v<FrameLightingData::DirectionalLightGpu>);
static_assert(std::is_standard_layout_v<FrameLightingData::PointLightGpu>);
static_assert(alignof(FrameLightingData) == 16);
static_assert(offsetof(FrameLightingData, directionalLightCount) == 0);
static_assert(offsetof(FrameLightingData, pointLightCount) == 4);
static_assert(offsetof(FrameLightingData, _pad1) == 8);
static_assert(offsetof(FrameLightingData, directionalLights) == 16);
static_assert(offsetof(FrameLightingData, pointLights) == 48);
static_assert(offsetof(FrameLightingData::DirectionalLightGpu, direction) == 0);
static_assert(offsetof(FrameLightingData::DirectionalLightGpu, _pad0) == 12);
static_assert(offsetof(FrameLightingData::DirectionalLightGpu, color) == 16);
static_assert(offsetof(FrameLightingData::DirectionalLightGpu, intensity) == 28);
static_assert(offsetof(FrameLightingData::PointLightGpu, position) == 0);
static_assert(offsetof(FrameLightingData::PointLightGpu, range) == 12);
static_assert(offsetof(FrameLightingData::PointLightGpu, color) == 16);
static_assert(offsetof(FrameLightingData::PointLightGpu, intensity) == 28);
static_assert(sizeof(FrameLightingData::DirectionalLightGpu) == 32);
static_assert(sizeof(FrameLightingData::PointLightGpu) == 32);
static_assert(sizeof(FrameLightingData) == 176);
static_assert(alignof(FrameLightingData::DirectionalLightGpu) == 16);
static_assert(alignof(FrameLightingData::PointLightGpu) == 16);

// ADR-0062's own Accepted Amendment (Plan 0023 Milestone 2): a new,
// separate, tail-only struct appended after the existing, completely
// unmodified 304-byte CameraMatrices+FrameLightingData region -- never
// widening either of them. Explicit tail pad, matching
// DirectionalLightGpu's own identical _pad0 convention -- never
// compiler-implicit.
struct alignas(16) CameraWorldPositionData {
  float x = 0.0f;  // offset 0 (buffer offset 304)
  float y = 0.0f;
  float z = 0.0f;
  float _pad = 0.0f;  // offset 12 (buffer offset 316) -- explicit, not implicit
};
static_assert(std::is_standard_layout_v<CameraWorldPositionData>);
static_assert(alignof(CameraWorldPositionData) == 16);
static_assert(offsetof(CameraWorldPositionData, x) == 0);
static_assert(offsetof(CameraWorldPositionData, y) == 4);
static_assert(offsetof(CameraWorldPositionData, z) == 8);
static_assert(offsetof(CameraWorldPositionData, _pad) == 12);
static_assert(sizeof(CameraWorldPositionData) == 16);

// Plan 0019 Section P8: a deliberate, disclosed, narrow break from this
// file's own "raw values only, no atlantis::world:: type" style --
// atlantis::runtime (Runtime) already depends on Atlantis::World
// throughout runtime_application.cpp, so this introduces no new
// module-boundary dependency, only a new dependency within an
// already-fully-dependent module's own internal header. The one place
// this header names an atlantis::world:: type.
struct LightExtractionInput {
  atlantis::world::Light light;  // the entity's own current Light component
  Mat4 worldMatrix;              // the entity's own current world matrix
};

// A direct transcription of Spec 0019 D2: iterates lights once, in the
// caller-supplied order (the caller -- World::lightEntities()'s own
// ascending-slot-index order -- is responsible for that order; this
// function performs no reordering of its own, matching
// computePendingMaterialIds()'s own "order is the caller's
// responsibility" precedent). At most one Directional light and up to
// four Point lights are ever written -- Plan 0019 Section P8's own
// TooManyLights semantics: a lights vector violating either cap is a
// programmer error (unreachable from any real cook/decode-validated
// scene, both of which already independently cap this count), so this
// fails fast via ATLANTIS_CHECK_MSG (always evaluated, Debug and
// Release alike -- assert.h), never a Result::Err, silent truncation,
// or out-of-bounds write.
[[nodiscard]] atlantis::Result<FrameLightingData, SceneExtractionError> extractFrameLightingData(
    const std::vector<LightExtractionInput>& lights);

// Plan 0019 Section P8/P15: a per-entity, per-frame check, called only
// for a LitTextured-bound entity's own current world matrix --
// unrelated to light extraction itself, grouped here only because it
// shares this file's own "world-matrix-driven, SceneExtractionError-
// returning" shape. Accepts an upper-left 3x3 that is a pure rotation,
// optionally combined with a uniform scale of either sign (D7's own
// "conformal" definition); rejects non-uniform scale, shear, or a
// degenerate (near-zero-length) column.
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> checkConformalTransform(const Mat4& worldMatrix);

// Plan 0019 Section P14: the one, single C++-side definition -- see
// shaders/lit_textured/lit_textured.slang's own
// `static const float kPointLightDistanceEpsilon = 1e-4;` (P11), a
// second, independent, hand-kept-in-sync literal (C++ and Slang cannot
// share a single defining header). A stated, accepted
// single-source-of-truth risk, not a solved problem -- matching
// descriptor_contract.h's own identical class of disclosed duplication.
inline constexpr float kPointLightDistanceEpsilon = 1e-4f;

// Plan 0023 Milestone 7 (ADR-0067 D-1/D-2): the one, single C++-side
// definitions -- see shaders/pbr_direct_lit/pbr_direct_lit.slang's own
// identical `static const` declarations (Milestone 4), second,
// independent, hand-kept-in-sync literals, matching
// kPointLightDistanceEpsilon's own identical, already-disclosed
// single-source-of-truth risk above.
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kMinAlpha = 1e-3f;
inline constexpr float kMinDot = 1e-4f;

// A direct, line-for-line C++ transcription of lit_textured.slang's own
// fragmentMain() accumulation loop (P11) -- literal formula parity is
// the entire point of this function's own existence, verified by
// Milestone 7's own hand-computed-expected-value unit tests AND,
// independently, by the golden's own per-pixel cross-check (Milestone
// 10). Never called by any real rendering path -- this is a
// test-and-verification-only CPU mirror of GPU-side math, not a second,
// parallel lighting implementation this codebase now has to keep
// working.
[[nodiscard]] Vec3 computeLambertianDiffuse(const Vec3& worldPosition, const Vec3& worldNormal,
                                             const FrameLightingData& lighting);

// Plan 0023 Milestone 7 (ADR-0067 D-1/D-2): a direct, line-for-line C++
// transcription of pbr_direct_lit.slang's own fragmentMain() BRDF
// accumulation loop -- literal formula parity is the entire point of
// this function's own existence, independently verified by hand-
// computed-expected-value unit tests, never by calling the shader's own
// compiled output or sharing any helper with it. Mirrors
// computeLambertianDiffuse()'s own established convention exactly:
// texture sampling and the final clamp are NOT part of this function
// (the caller's own concern) -- baseColor is already
// texColor.rgb * baseColorFactor.rgb, matching the shader's own
// pre-accumulation step; this returns only the accumulated
// per-fragment lighting contribution, unclamped. Never called by any
// real rendering path -- test-and-verification-only, like
// computeLambertianDiffuse() itself.
[[nodiscard]] Vec3 computePbrDirectLighting(const Vec3& worldPosition, const Vec3& worldNormal,
                                             const Vec3& cameraWorldPosition, const Vec3& baseColor,
                                             float metallicFactor, float roughnessFactor,
                                             const FrameLightingData& lighting);

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

// Plan 0027 (ADR-0072 D-1/P4/P11): the one directional light's own fixed
// light-space view/projection -- Runtime's runFrame() and the shadow
// discriminator tests both call this, never a separately maintained
// copy. `direction` is the light's own travel direction (unit-length,
// already normalized by extractFrameLightingData()). Unlike
// extractCameraMatrices() above, this never fails: a direction
// (near-)parallel to world-up is an ordinary "sun overhead" case, not a
// scene-authoring mistake, so the up-vector falls back to (0,0,1)
// instead of erroring -- reusing the exact same cross-product-length
// test and kDegenerateLengthEpsilon threshold extractCameraMatrices()
// already established, just with a different response to degeneracy.
// P4's own fixed orthographic volume (center (0,0,0), half-extent 8.0,
// near 0.1, far 30.0) is applied uniformly, never scene-fitted.
[[nodiscard]] CameraMatrices computeShadowLightSpaceMatrices(const Vec3& direction);

// Plan 0023 Milestone 2 (ADR-0062's own Accepted Amendment): a small,
// separate function -- never a widening of extractCameraMatrices()'s
// own return type, which stays exactly CameraMatrices, unchanged. Reads
// the same column-12/13/14 translation extractCameraMatrices() already
// derives internally as its own local `eye` (scene_extraction.cpp), but
// as an independent, infallible extraction -- the camera's own world
// matrix is already known-valid by the time this is called (the same
// cameraWorldMatrix extractCameraMatrices() itself validates), so this
// function never fails.
[[nodiscard]] CameraWorldPositionData extractCameraWorldPosition(const Mat4& cameraWorldMatrix);

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

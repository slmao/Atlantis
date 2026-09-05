#include <atlantis/runtime/scene_extraction.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <cmath>

namespace atlantis::runtime {

namespace {

constexpr float kDegenerateLengthEpsilon = 1e-6f;  // this codebase's own scenes operate at a roughly
                                                     // 1-10 world-unit scale -- see Plan 0014 Deviations
                                                     // for why this exact value is not further tuned

// Plan 0019 Section P7: this file's own Vec3 struct moved to
// scene_extraction.h (now atlantis::runtime::Vec3) -- these helpers
// operate on that single, public definition.
[[nodiscard]] float length(const Vec3& v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] float dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 normalize(const Vec3& v, float len) {
  return {v.x / len, v.y / len, v.z / len};
}

// Plan 0023 Milestone 7: small Vec3 arithmetic helpers, added alongside
// length()/cross()/dot()/normalize() above -- computePbrDirectLighting()'s
// own BRDF math (ADR-0067 D-1) is significantly more component-wise
// arithmetic than computeLambertianDiffuse()'s, so these keep it
// readable without introducing operator overloads Vec3 itself does not
// declare.
[[nodiscard]] Vec3 vecAdd(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 vecSub(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 vecScale(const Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] Vec3 vecMul(const Vec3& a, const Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

[[nodiscard]] Vec3 vecLerp(const Vec3& a, const Vec3& b, float t) {
  return vecAdd(vecScale(a, 1.0f - t), vecScale(b, t));
}

}  // namespace

Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// Duplicated -- not shared -- from every other composition root's own
// copy (examples/minimal_renderer_demo, minimal_cube_fixture.cpp,
// runtime_application.cpp before this file), matching this codebase's
// own long-standing "duplicated, not shared" precedent for exactly this
// kind of small camera-math helper.
Mat4 lookAtMatrix(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ) {
  float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
  const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= fLen;
  fy /= fLen;
  fz /= fLen;

  const float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
  float sx = fy * upZ - fz * upY;
  float sy = fz * upX - fx * upZ;
  float sz = fx * upY - fy * upX;
  const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz);
  sx /= sLen;
  sy /= sLen;
  sz /= sLen;

  const float ux = sy * fz - sz * fy;
  const float uy = sz * fx - sx * fz;
  const float uz = sx * fy - sy * fx;

  Mat4 result = identityMatrix();
  result[0] = sx;
  result[4] = sy;
  result[8] = sz;
  result[1] = ux;
  result[5] = uy;
  result[9] = uz;
  result[2] = -fx;
  result[6] = -fy;
  result[10] = -fz;
  result[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
  result[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
  result[14] = fx * eyeX + fy * eyeY + fz * eyeZ;
  return result;
}

// Vulkan clip-space convention: right-handed, depth range [0, 1], Y
// flipped relative to the classic OpenGL convention.
Mat4 perspectiveMatrix(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

atlantis::Result<CameraMatrices, SceneExtractionError> extractCameraMatrices(const Mat4& cameraWorldMatrix,
                                                                              float fovYRadians, float nearZ,
                                                                              float farZ, float aspect) {
  // (1) forward = normalize(-column 2 of cameraWorldMatrix).
  const Vec3 negColumn2{-cameraWorldMatrix[8], -cameraWorldMatrix[9], -cameraWorldMatrix[10]};
  const float forwardLen = length(negColumn2);
  if (forwardLen < kDegenerateLengthEpsilon) {
    return atlantis::Result<CameraMatrices, SceneExtractionError>::Err(SceneExtractionError::DegenerateCameraForward);
  }
  const Vec3 forward{negColumn2.x / forwardLen, negColumn2.y / forwardLen, negColumn2.z / forwardLen};

  // (2) eye = column 3 (translation) of cameraWorldMatrix.
  const Vec3 eye{cameraWorldMatrix[12], cameraWorldMatrix[13], cameraWorldMatrix[14]};

  // (3) right = cross(forward, world-up); degenerate if forward is
  // (near-)parallel to world-up. This check runs before calling
  // lookAtMatrix(), which performs the identical cross product
  // internally without a guard.
  const Vec3 worldUp{0.0f, 1.0f, 0.0f};
  const Vec3 right = cross(forward, worldUp);
  if (length(right) < kDegenerateLengthEpsilon) {
    return atlantis::Result<CameraMatrices, SceneExtractionError>::Err(SceneExtractionError::DegenerateCameraBasis);
  }

  // (4) On success.
  CameraMatrices result;
  result.view = lookAtMatrix(eye.x, eye.y, eye.z, eye.x + forward.x, eye.y + forward.y, eye.z + forward.z);
  result.projection = perspectiveMatrix(fovYRadians, aspect, nearZ, farZ);
  return atlantis::Result<CameraMatrices, SceneExtractionError>::Ok(result);
}

namespace {

// P4's own fixed orthographic shadow volume: center (0,0,0), half-extent
// 8.0 world units, near 0.1, far 30.0 -- applied uniformly, never
// scene-fitted.
constexpr float kShadowOrthographicHalfExtent = 8.0f;
constexpr float kShadowNearZ = 0.1f;
constexpr float kShadowFarZ = 30.0f;

}  // namespace

CameraMatrices computeShadowLightSpaceMatrices(const Vec3& direction) {
  // P11: a deliberate, disclosed new choice, not a reuse of
  // lookAtMatrix()'s own hardcoded world-up -- fail-fast (as
  // extractCameraMatrices() above does for a degenerate camera basis)
  // is not appropriate here, so this falls back to a second fixed
  // up-vector instead of erroring.
  Vec3 up{0.0f, 1.0f, 0.0f};
  if (length(cross(direction, up)) < kDegenerateLengthEpsilon) {
    up = Vec3{0.0f, 0.0f, 1.0f};
  }

  // light eye = center - direction * (far - near) / 2 (center is the
  // fixed volume's own origin, so this reduces to eye = -direction *
  // (far - near) / 2); light forward = direction.
  const float eyeDistance = (kShadowFarZ - kShadowNearZ) * 0.5f;
  const Vec3 eye{-direction.x * eyeDistance, -direction.y * eyeDistance, -direction.z * eyeDistance};

  const Vec3 right = cross(direction, up);
  const float rightLen = length(right);
  const Vec3 rightUnit = normalize(right, rightLen);
  const Vec3 camUp = cross(rightUnit, direction);

  CameraMatrices result;
  result.view = identityMatrix();
  result.view[0] = rightUnit.x;
  result.view[4] = rightUnit.y;
  result.view[8] = rightUnit.z;
  result.view[1] = camUp.x;
  result.view[5] = camUp.y;
  result.view[9] = camUp.z;
  result.view[2] = -direction.x;
  result.view[6] = -direction.y;
  result.view[10] = -direction.z;
  result.view[12] = -dot(rightUnit, eye);
  result.view[13] = -dot(camUp, eye);
  result.view[14] = dot(direction, eye);

  // Vulkan clip-space convention: right-handed, depth range [0, 1], Y
  // flipped -- mirrors perspectiveMatrix()'s own identical convention,
  // applied to a fixed, symmetric orthographic volume instead of a
  // perspective frustum.
  result.projection = Mat4{};
  result.projection[0] = 1.0f / kShadowOrthographicHalfExtent;
  result.projection[5] = -1.0f / kShadowOrthographicHalfExtent;
  result.projection[10] = -1.0f / (kShadowFarZ - kShadowNearZ);
  result.projection[14] = -kShadowNearZ / (kShadowFarZ - kShadowNearZ);
  result.projection[15] = 1.0f;

  return result;
}

CameraWorldPositionData extractCameraWorldPosition(const Mat4& cameraWorldMatrix) {
  // Mirrors extractCameraMatrices()'s own `eye` derivation above (column
  // 3 / indices 12,13,14) -- deliberately independent, not shared
  // through a common helper, matching this function's own infallible
  // contract (extractCameraMatrices() may fail on a degenerate forward
  // or basis; a plain translation read never can).
  CameraWorldPositionData result;
  result.x = cameraWorldMatrix[12];
  result.y = cameraWorldMatrix[13];
  result.z = cameraWorldMatrix[14];
  return result;
}

atlantis::Result<std::monostate, SceneExtractionError> resolveMeshAsset(
    atlantis::asset_system::AssetId requested, const std::vector<atlantis::asset_system::AssetId>& knownIds) {
  if (std::find(knownIds.begin(), knownIds.end(), requested) == knownIds.end()) {
    return atlantis::Result<std::monostate, SceneExtractionError>::Err(SceneExtractionError::UnresolvedMeshAsset);
  }
  return atlantis::Result<std::monostate, SceneExtractionError>::Ok({});
}

atlantis::Result<std::monostate, SceneExtractionError> resolveMaterialAsset(
    atlantis::asset_system::AssetId requested, const std::vector<atlantis::asset_system::AssetId>& knownIds) {
  if (std::find(knownIds.begin(), knownIds.end(), requested) == knownIds.end()) {
    return atlantis::Result<std::monostate, SceneExtractionError>::Err(SceneExtractionError::UnresolvedMaterialAsset);
  }
  return atlantis::Result<std::monostate, SceneExtractionError>::Ok({});
}

// Plan 0019 Section P8: a direct transcription of Spec 0019 D2. Iterates
// `lights` once, in the caller-supplied order, writing the first
// Directional entry into directionalLights[0] (identical
// -column2/normalize/degenerate-check formula extractCameraMatrices()
// uses for a camera's own forward vector, reusing the same
// kDegenerateLengthEpsilon constant verbatim) and every Point entry
// (up to four) into pointLights[] via its own translation column
// (column 3, always well-defined -- no degenerate case for a raw
// position). A lights vector violating either cap (a second Directional,
// a fifth Point) is a programmer error -- unreachable from any real
// cook/decode-validated scene, both of which already independently cap
// this count (P3/P4) -- so ATLANTIS_CHECK_MSG aborts immediately, in
// both Debug and Release, before either fixed-size array could ever be
// written past.
atlantis::Result<FrameLightingData, SceneExtractionError> extractFrameLightingData(
    const std::vector<LightExtractionInput>& lights) {
  FrameLightingData data{};

  for (const LightExtractionInput& input : lights) {
    if (input.light.kind == atlantis::world::LightKind::Directional) {
      if (data.directionalLightCount >= 1) {
        // Reached only if a caller bypasses both real gates (P3's
        // parseSceneSource(), P4's decodeSceneArtifact()) entirely --
        // e.g. hand-constructed World::setLight() calls, in a test or
        // some future, not-yet-existing code path. ATLANTIS_CHECK_MSG's
        // default handler aborts the process here, in both Debug and
        // Release (assert.h); the `continue` below is a defense-in-depth
        // guard so that even a caller that has replaced the failure
        // handler with a non-aborting one (assertions::setFailureHandler())
        // still can never write past directionalLights[0] -- the
        // "never an out-of-bounds write" guarantee holds unconditionally,
        // not only under the default handler.
        ATLANTIS_CHECK_MSG(false,
                            "extractFrameLightingData(): a second Directional light reached this function -- "
                            "both parseSceneSource() (P3) and decodeSceneArtifact() (P4) already cap this at "
                            "one; this is a programmer error (e.g. World::setLight() called directly, "
                            "bypassing both gates), not a recoverable runtime condition");
        continue;
      }
      const Mat4& m = input.worldMatrix;
      const Vec3 negColumn2{-m[8], -m[9], -m[10]};
      const float len = length(negColumn2);
      if (len < kDegenerateLengthEpsilon) {
        return atlantis::Result<FrameLightingData, SceneExtractionError>::Err(
            SceneExtractionError::DegenerateLightDirection);
      }
      const Vec3 direction = normalize(negColumn2, len);
      FrameLightingData::DirectionalLightGpu& gpu = data.directionalLights[0];
      gpu.direction[0] = direction.x;
      gpu.direction[1] = direction.y;
      gpu.direction[2] = direction.z;
      gpu.color[0] = input.light.color.x;
      gpu.color[1] = input.light.color.y;
      gpu.color[2] = input.light.color.z;
      gpu.intensity = input.light.intensity;
      data.directionalLightCount = 1;
    } else {
      if (data.pointLightCount >= 4) {
        // Identical reasoning to the Directional branch above, applied
        // to the four-element pointLights[] bound.
        ATLANTIS_CHECK_MSG(false,
                            "extractFrameLightingData(): a fifth Point light reached this function -- both "
                            "parseSceneSource() (P3) and decodeSceneArtifact() (P4) already cap this at four; "
                            "this is a programmer error (e.g. World::setLight() called directly, bypassing "
                            "both gates), not a recoverable runtime condition");
        continue;
      }
      const Mat4& m = input.worldMatrix;
      FrameLightingData::PointLightGpu& gpu = data.pointLights[data.pointLightCount];
      gpu.position[0] = m[12];
      gpu.position[1] = m[13];
      gpu.position[2] = m[14];
      gpu.range = input.light.range;
      gpu.color[0] = input.light.color.x;
      gpu.color[1] = input.light.color.y;
      gpu.color[2] = input.light.color.z;
      gpu.intensity = input.light.intensity;
      ++data.pointLightCount;
    }
  }

  return atlantis::Result<FrameLightingData, SceneExtractionError>::Ok(data);
}

// Plan 0019 Section P8/P15 (Spec 0019 D7): the upper-left 3x3 of
// worldMatrix is conformal iff it equals a uniform scale (either sign)
// times an orthogonal matrix -- equivalently, its three column vectors
// are pairwise orthogonal and of equal length. A uniform NEGATIVE scale
// (e.g. -1 * I, a full point reflection) still satisfies both: -I is
// itself an orthogonal matrix (its columns stay pairwise orthogonal and
// equal-length), matching D7's own explicitly-named "uniform scale of
// either sign" acceptance case.
atlantis::Result<std::monostate, SceneExtractionError> checkConformalTransform(const Mat4& worldMatrix) {
  const Vec3 col0{worldMatrix[0], worldMatrix[1], worldMatrix[2]};
  const Vec3 col1{worldMatrix[4], worldMatrix[5], worldMatrix[6]};
  const Vec3 col2{worldMatrix[8], worldMatrix[9], worldMatrix[10]};

  const float len0 = length(col0);
  const float len1 = length(col1);
  const float len2 = length(col2);
  // Reuses the identical near-zero-length numerical boundary
  // DegenerateLightDirection's own check already exercises for a
  // different matrix column (P15's own explicit "consistent epsilon
  // treatment" requirement).
  if (len0 < kDegenerateLengthEpsilon || len1 < kDegenerateLengthEpsilon || len2 < kDegenerateLengthEpsilon) {
    return atlantis::Result<std::monostate, SceneExtractionError>::Err(
        SceneExtractionError::NonConformalNormalTransform);
  }

  // A 1% relative tolerance on squared length/dot-product magnitudes --
  // an Implementation-time closure (disclosed here, not silently
  // widened) accommodating ordinary floating-point matrix composition
  // error for genuinely conformal inputs, while still rejecting every
  // hand-constructed non-uniform-scale/shear test case (Milestone 7/V12)
  // by a comfortable margin.
  constexpr float kRelativeToleranceSquared = 1e-4f;

  const float len0Sq = len0 * len0;
  const float len1Sq = len1 * len1;
  const float len2Sq = len2 * len2;
  const float avgLenSq = (len0Sq + len1Sq + len2Sq) / 3.0f;
  const float lengthTolerance = kRelativeToleranceSquared * avgLenSq;

  const bool lengthsEqual = std::abs(len0Sq - avgLenSq) <= lengthTolerance &&
                             std::abs(len1Sq - avgLenSq) <= lengthTolerance &&
                             std::abs(len2Sq - avgLenSq) <= lengthTolerance;

  const bool orthogonal = std::abs(dot(col0, col1)) <= lengthTolerance &&
                           std::abs(dot(col0, col2)) <= lengthTolerance &&
                           std::abs(dot(col1, col2)) <= lengthTolerance;

  if (!lengthsEqual || !orthogonal) {
    return atlantis::Result<std::monostate, SceneExtractionError>::Err(
        SceneExtractionError::NonConformalNormalTransform);
  }
  return atlantis::Result<std::monostate, SceneExtractionError>::Ok({});
}

// Plan 0019 Section P14: a direct, line-for-line C++ transcription of
// lit_textured.slang's own fragmentMain() accumulation loop (P11) --
// the texture-color multiply and the final clamp are deliberately NOT
// part of this function (they are the caller's own concern, mirroring
// exactly how the shader itself applies them only once, after this
// accumulation, never per-light) -- this returns only the accumulated
// per-fragment lighting contribution.
Vec3 computeLambertianDiffuse(const Vec3& worldPosition, const Vec3& worldNormal, const FrameLightingData& lighting) {
  const Vec3 N = normalize(worldNormal, length(worldNormal));
  Vec3 accumulated{0.0f, 0.0f, 0.0f};  // no ambient term -- explicit, see D6

  for (std::uint32_t i = 0; i < lighting.directionalLightCount; ++i) {
    const FrameLightingData::DirectionalLightGpu& dl = lighting.directionalLights[i];
    const Vec3 L{-dl.direction[0], -dl.direction[1], -dl.direction[2]};
    const float ndotl = std::max(dot(N, L), 0.0f);
    accumulated.x += dl.color[0] * dl.intensity * ndotl;
    accumulated.y += dl.color[1] * dl.intensity * ndotl;
    accumulated.z += dl.color[2] * dl.intensity * ndotl;
  }

  for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
    const FrameLightingData::PointLightGpu& pl = lighting.pointLights[j];
    const Vec3 toLight{pl.position[0] - worldPosition.x, pl.position[1] - worldPosition.y,
                        pl.position[2] - worldPosition.z};
    const float dist = std::max(length(toLight), kPointLightDistanceEpsilon);
    const Vec3 L = normalize(toLight, dist);
    const float ndotl = std::max(dot(N, L), 0.0f);
    const float atten = std::clamp(1.0f - dist / pl.range, 0.0f, 1.0f);
    accumulated.x += pl.color[0] * pl.intensity * ndotl * atten;
    accumulated.y += pl.color[1] * pl.intensity * ndotl * atten;
    accumulated.z += pl.color[2] * pl.intensity * ndotl * atten;
  }

  return accumulated;
}

// Plan 0023 Milestone 7 (ADR-0067 D-1/D-2): a direct, line-for-line C++
// transcription of pbr_direct_lit.slang's own fragmentMain() BRDF
// accumulation loop -- see this function's own header comment for the
// full contract (texture sampling/final clamp are the caller's own
// concern, matching computeLambertianDiffuse()'s own convention).
Vec3 computePbrDirectLighting(const Vec3& worldPosition, const Vec3& worldNormal, const Vec3& cameraWorldPosition,
                               const Vec3& baseColor, float metallicFactor, float roughnessFactor,
                               const FrameLightingData& lighting) {
  const float metallic = std::clamp(metallicFactor, 0.0f, 1.0f);
  const float roughness = std::clamp(roughnessFactor, 0.0f, 1.0f);
  const float alpha = std::max(roughness * roughness, kMinAlpha);
  const Vec3 F0 = vecLerp(Vec3{0.04f, 0.04f, 0.04f}, baseColor, metallic);
  const Vec3 diffuseColor = vecScale(baseColor, 1.0f - metallic);

  const Vec3 N = normalize(worldNormal, length(worldNormal));
  const Vec3 toCamera = vecSub(cameraWorldPosition, worldPosition);
  const Vec3 V = normalize(toCamera, length(toCamera));
  Vec3 accumulated{0.0f, 0.0f, 0.0f};  // no ambient term, matching every other MaterialKind

  // Shared per-light BRDF terms -- one lambda, called identically from
  // both loops below, matching pbr_direct_lit.slang's own duplication
  // of this same block per light kind (never factored into a shared
  // Slang function either, D-1's own pseudocode shape).
  const auto accumulateLight = [&](const Vec3& L, const Vec3& radiance) {
    const float NdotL = std::max(dot(N, L), 0.0f);
    if (NdotL <= 0.0f) return;  // branch, not a trailing multiply -- D/G/F can independently produce NaN

    const float NdotV = std::max(dot(N, V), kMinDot);
    const Vec3 H = normalize(vecAdd(L, V), length(vecAdd(L, V)));
    const float NdotH = std::max(dot(N, H), 0.0f);
    const float VdotH = std::max(dot(V, H), 0.0f);

    const float D = (alpha * alpha) / (kPi * std::pow(NdotH * NdotH * (alpha * alpha - 1.0f) + 1.0f, 2.0f));

    // Karis's own DIRECT-LIGHTING remap, using roughness directly --
    // never alpha (alpha/2 is his IBL remap, not used here).
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    const float G1V = NdotV / (NdotV * (1.0f - k) + k);
    const float G1L = NdotL / (NdotL * (1.0f - k) + k);
    const float G = G1V * G1L;

    const Vec3 F = vecAdd(F0, vecScale(vecSub(Vec3{1.0f, 1.0f, 1.0f}, F0), std::pow(1.0f - VdotH, 5.0f)));

    const float specularScalar = (D * G) / std::max(4.0f * NdotV * NdotL, kMinDot);
    const Vec3 specular = vecScale(F, specularScalar);
    const Vec3 kD = vecScale(vecSub(Vec3{1.0f, 1.0f, 1.0f}, F), 1.0f - metallic);

    const Vec3 diffuseTerm = vecScale(vecMul(kD, diffuseColor), 1.0f / kPi);
    const Vec3 lightContribution = vecScale(vecMul(vecAdd(diffuseTerm, specular), radiance), NdotL);
    accumulated = vecAdd(accumulated, lightContribution);
  };

  for (std::uint32_t i = 0; i < lighting.directionalLightCount; ++i) {
    const FrameLightingData::DirectionalLightGpu& dl = lighting.directionalLights[i];
    const Vec3 L{-dl.direction[0], -dl.direction[1], -dl.direction[2]};
    const Vec3 radiance = vecScale(Vec3{dl.color[0], dl.color[1], dl.color[2]}, dl.intensity);
    accumulateLight(L, radiance);
  }

  for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
    const FrameLightingData::PointLightGpu& pl = lighting.pointLights[j];
    const Vec3 toLight{pl.position[0] - worldPosition.x, pl.position[1] - worldPosition.y,
                        pl.position[2] - worldPosition.z};
    const float dist = std::max(length(toLight), kPointLightDistanceEpsilon);
    const Vec3 L = normalize(toLight, dist);
    const float atten = std::clamp(1.0f - dist / pl.range, 0.0f, 1.0f);
    const Vec3 radiance = vecScale(Vec3{pl.color[0], pl.color[1], pl.color[2]}, pl.intensity * atten);
    accumulateLight(L, radiance);
  }

  return accumulated;
}

}  // namespace atlantis::runtime

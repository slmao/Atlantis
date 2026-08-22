#include <atlantis/runtime/scene_extraction.h>

#include <cmath>

namespace atlantis::runtime {

namespace {

constexpr float kDegenerateLengthEpsilon = 1e-6f;  // this codebase's own scenes operate at a roughly
                                                     // 1-10 world-unit scale -- see Plan 0014 Deviations
                                                     // for why this exact value is not further tuned

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

[[nodiscard]] float length(const Vec3& v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
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

atlantis::Result<std::monostate, SceneExtractionError> resolveMeshAsset(atlantis::asset_system::AssetId requested,
                                                                          atlantis::asset_system::AssetId known) {
  if (requested != known) {
    return atlantis::Result<std::monostate, SceneExtractionError>::Err(SceneExtractionError::UnresolvedMeshAsset);
  }
  return atlantis::Result<std::monostate, SceneExtractionError>::Ok({});
}

}  // namespace atlantis::runtime

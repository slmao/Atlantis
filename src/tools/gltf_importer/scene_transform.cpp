#include "scene_transform.h"

#include <algorithm>
#include <cmath>

namespace atlantis::gltf_importer {

namespace {

// Row-major 3x3 view of a rotation: r[row][col].
using Mat3 = std::array<std::array<double, 3>, 3>;

// Inverse of R = Ry(y) * Rx(x) * Rz(z), whose entries are
//   r01 = -cy*sz + sy*sx*cz   r02 = sy*cx
//   r10 = cx*sz   r11 = cx*cz   r12 = -sx
//   r20 = -sy*cz + cy*sx*sz   r22 = cy*cx   r00 = cy*cz + sy*sx*sz
// so pitch = asin(-r12), yaw = atan2(r02, r22), roll = atan2(r10, r11).
// When cos(pitch) ~ 0 yaw and roll are coupled; roll is fixed to 0 and yaw
// read from r00 = cy, r20 = -sy.
[[nodiscard]] std::array<double, 3> eulerFromRotation(const Mat3& r) {
  constexpr double kGimbalEpsilon = 1e-9;
  const double sinPitch = std::clamp(-r[1][2], -1.0, 1.0);
  const double pitch = std::asin(sinPitch);
  if (1.0 - std::abs(sinPitch) < kGimbalEpsilon) {
    return {pitch, std::atan2(-r[2][0], r[0][0]), 0.0};
  }
  return {pitch, std::atan2(r[0][2], r[2][2]), std::atan2(r[1][0], r[1][1])};
}

}  // namespace

std::array<double, 3> eulerFromQuaternion(const std::array<double, 4>& xyzw) noexcept {
  const double norm = std::sqrt(xyzw[0] * xyzw[0] + xyzw[1] * xyzw[1] + xyzw[2] * xyzw[2] + xyzw[3] * xyzw[3]);
  const double x = xyzw[0] / norm, y = xyzw[1] / norm, z = xyzw[2] / norm, w = xyzw[3] / norm;
  const Mat3 r = {{
      {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)},
      {2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)},
      {2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)},
  }};
  return eulerFromRotation(r);
}

atlantis::Result<DecomposedTransform, GltfImportError> decomposeMatrix(const std::array<double, 16>& m) noexcept {
  using ResultT = atlantis::Result<DecomposedTransform, GltfImportError>;
  constexpr double kAffineEpsilon = 1e-6;
  constexpr double kZeroScaleEpsilon = 1e-12;
  // Tolerance on the cosine between normalized basis columns: glTF matrices
  // are float, so exact orthogonality cannot be demanded, but anything
  // larger is a real shear that T*R*S cannot represent.
  constexpr double kShearEpsilon = 1e-4;

  for (const double v : m) {
    if (!std::isfinite(v)) return ResultT::Err(GltfImportError::NonDecomposableMatrix);
  }
  if (std::abs(m[3]) > kAffineEpsilon || std::abs(m[7]) > kAffineEpsilon || std::abs(m[11]) > kAffineEpsilon ||
      std::abs(m[15] - 1.0) > kAffineEpsilon) {
    return ResultT::Err(GltfImportError::NonDecomposableMatrix);
  }

  std::array<std::array<double, 3>, 3> columns = {{{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {m[8], m[9], m[10]}}};
  DecomposedTransform result;
  result.translation = {m[12], m[13], m[14]};
  for (int c = 0; c < 3; ++c) {
    const double length =
        std::sqrt(columns[c][0] * columns[c][0] + columns[c][1] * columns[c][1] + columns[c][2] * columns[c][2]);
    if (length < kZeroScaleEpsilon) return ResultT::Err(GltfImportError::NonDecomposableMatrix);
    result.scale[c] = length;
    for (double& v : columns[c]) v /= length;
  }
  auto dot = [&](int a, int b) {
    return columns[a][0] * columns[b][0] + columns[a][1] * columns[b][1] + columns[a][2] * columns[b][2];
  };
  if (std::abs(dot(0, 1)) > kShearEpsilon || std::abs(dot(0, 2)) > kShearEpsilon || std::abs(dot(1, 2)) > kShearEpsilon) {
    return ResultT::Err(GltfImportError::NonDecomposableMatrix);
  }
  const double det = columns[0][0] * (columns[1][1] * columns[2][2] - columns[1][2] * columns[2][1]) -
                     columns[1][0] * (columns[0][1] * columns[2][2] - columns[0][2] * columns[2][1]) +
                     columns[2][0] * (columns[0][1] * columns[1][2] - columns[0][2] * columns[1][1]);
  if (det < 0.0) return ResultT::Err(GltfImportError::NegativeDeterminant);

  Mat3 r{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) r[row][col] = columns[col][row];
  }
  result.eulerRadians = eulerFromRotation(r);
  return ResultT::Ok(result);
}

std::array<double, 16> composeTransform(const DecomposedTransform& t) noexcept {
  const double cx = std::cos(t.eulerRadians[0]), sx = std::sin(t.eulerRadians[0]);
  const double cy = std::cos(t.eulerRadians[1]), sy = std::sin(t.eulerRadians[1]);
  const double cz = std::cos(t.eulerRadians[2]), sz = std::sin(t.eulerRadians[2]);
  const Mat3 r = {{
      {cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx},
      {cx * sz, cx * cz, -sx},
      {-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx},
  }};
  std::array<double, 16> out{};
  for (int col = 0; col < 3; ++col) {
    for (int row = 0; row < 3; ++row) out[col * 4 + row] = r[row][col] * t.scale[col];
  }
  out[12] = t.translation[0];
  out[13] = t.translation[1];
  out[14] = t.translation[2];
  out[15] = 1.0;
  return out;
}

}  // namespace atlantis::gltf_importer

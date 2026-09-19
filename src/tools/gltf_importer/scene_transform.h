#pragma once

#include "import_command.h"

#include <atlantis/result.h>

#include <array>

namespace atlantis::gltf_importer {

// Plan 0037 D6: glTF and Atlantis share every convention (right-handed,
// +Y up, column vectors, local = T * R * S -- ADR-0050), so no axis or
// handedness conversion is applied; only the rotation's representation
// changes, from a unit quaternion to Atlantis's Euler angles
// (pitch x, yaw y, roll z) composed as R = Ry(yaw) * Rx(pitch) * Rz(roll).
// Pure and thread-safe.

// Euler angles (x = pitch, y = yaw, z = roll), radians, for the rotation of
// quaternion (x, y, z, w), which must be finite and non-zero; it is
// normalized first. At gimbal lock (|pitch| = pi/2) roll is 0.
[[nodiscard]] std::array<double, 3> eulerFromQuaternion(const std::array<double, 4>& xyzw) noexcept;

struct DecomposedTransform {
  std::array<double, 3> translation{0.0, 0.0, 0.0};
  std::array<double, 3> eulerRadians{0.0, 0.0, 0.0};
  std::array<double, 3> scale{1.0, 1.0, 1.0};
};

// Splits a glTF node matrix (column-major, 16 floats) into T, R (as Euler)
// and S. Fails with NonDecomposableMatrix for a non-finite, non-affine,
// zero-scale or sheared matrix, and NegativeDeterminant for a mirroring one.
[[nodiscard]] atlantis::Result<DecomposedTransform, GltfImportError> decomposeMatrix(
    const std::array<double, 16>& columnMajor) noexcept;

// T * Ry * Rx * Rz * S, column-major -- ADR-0050's composition, used to
// verify decompositions and by the tests.
[[nodiscard]] std::array<double, 16> composeTransform(const DecomposedTransform& transform) noexcept;

}  // namespace atlantis::gltf_importer

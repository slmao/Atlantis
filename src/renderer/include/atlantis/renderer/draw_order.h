#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace atlantis::renderer {

// Plan 0042 Milestone 3 (Spec 0042 R7, ADR-0090 Decision 2, Plan 0042 P3):
// the plain-data view of one draw item that the draw order depends on --
// deliberately not a DrawItem, so the ordering is unit-testable without a
// Device, Pipeline or Material. drawFrame() projects each DrawItem into one
// of these: blended is Material::alphaMode() == Blend, worldSortPoint is
// objectToWorld applied to Mesh::localBoundsCentre().
struct DrawSortInput {
  bool blended = false;
  std::array<float, 3> worldSortPoint{};
};

// Returns every index of `items` exactly once, in draw order: first every
// non-blended item in input order, then every blended item farthest-first
// by squared distance from cameraWorldPosition to its worldSortPoint, equal
// distances keeping input order (a stable sort, so the result is
// deterministic). Pure; not thread-affine.
//
// Precondition (ATLANTIS_CHECK_MSG): cameraWorldPosition has a value
// whenever any item is blended. With no value and no blended item the
// camera is never read, so opaque-only callers need not supply one. If the
// check is handled without aborting (a test failure handler), the blended
// items keep input order.
[[nodiscard]] std::vector<std::uint32_t> computeDrawOrder(
    std::span<const DrawSortInput> items, const std::optional<std::array<float, 3>>& cameraWorldPosition);

}  // namespace atlantis::renderer

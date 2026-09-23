#pragma once

#include <array>
#include <cstddef>

#include <atlantis/rhi/types.h>

namespace atlantis::renderer {

// Plan 0042 Milestone 3 (Spec 0042 O4, ADR-0090 Decision 2, Plan 0042 P4 /
// Q2 option A): the local-space axis-aligned bounds centre of a vertex
// buffer, (min + max) / 2 per axis over every vertex's position. The
// position is the layout's location-0 attribute, read as three floats at
// its offsetBytes within each strideBytes-sized vertex -- nothing else in
// the layout is read. Returns (0, 0, 0) for zero vertices.
//
// Preconditions (ATLANTIS_CHECK_MSG, programmer errors -- never a silent
// origin): layout.strideBytes > 0; vertexDataSizeBytes is a multiple of
// it; the layout has a location-0 attribute whose format is Float3 and
// which fits inside the stride. If a check is handled without aborting (a
// test failure handler), the result is (0, 0, 0).
[[nodiscard]] std::array<float, 3> computeLocalBoundsCentre(const atlantis::rhi::VertexInputLayout& layout,
                                                            const void* vertexData, std::size_t vertexDataSizeBytes);

}  // namespace atlantis::renderer

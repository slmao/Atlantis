#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/result.h>

#include <vector>

namespace atlantis::asset_system {

// Plan 0029 Section P1 / ADR-0073 Decision items 3-5 (as amended by
// ADR-0073's own Accepted Correction, 2026-09-06): one tangent per
// mesh-source vertex, deterministically derived from its own
// position/UV0/normal/index data -- a standard per-triangle
// UV-Jacobian accumulation (Lengyel's method), never MikkTSpace's own
// more elaborate per-vertex weighting/averaging scheme.
struct VertexTangent {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

// Returns CookError directly (the same enum cookStaticMesh() already
// returns) -- CookError::DegenerateTangentBasis/TangentHandednessConflict
// are themselves CookError values per ADR-0073's own Decision. A whole-
// mesh failure never yields a partial result.
[[nodiscard]] atlantis::Result<std::vector<VertexTangent>, CookError> generateTangents(
    const ParsedMeshSource& source);

}  // namespace atlantis::asset_system

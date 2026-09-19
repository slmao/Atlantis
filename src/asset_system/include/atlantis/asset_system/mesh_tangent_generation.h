#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
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
// returns). The one whole-mesh failure is CookError::
// TangentHandednessConflict (ADR-0073 item 5); a whole-mesh failure never
// yields a partial result. Since ADR-0073's Amendment of 2026-09-19 a vertex
// whose accumulated tangent orthogonalizes to (near) zero takes the item 4a
// axis fallback instead, so CookError::DegenerateTangentBasis is no longer
// returned.
[[nodiscard]] atlantis::Result<std::vector<VertexTangent>, CookError> generateTangents(
    const ParsedMeshSource& source);

// Plan 0037 (D8): the uint32_t-index overload for schema-5 (.amesh v5)
// import paths. One algorithm, two entry points -- both delegate to the
// same Lengfel-body core (ADR-0073), so the existing u16 entry point's
// behavior is byte-identical by construction; the existing
// generateTangents(ParsedMeshSource) keeps serving the existing cooker
// unchanged.
//
// stats, when non-null, receives how many vertices took the item 4a axis
// fallback, split by cause (the glTF importer reports these per mesh).
struct TangentGenerationStats {
  std::size_t zeroContributionFallbacks = 0;  // no non-degenerate contributing triangle
  std::size_t degenerateBasisFallbacks = 0;   // contributions that orthogonalize to (near) zero
};

[[nodiscard]] atlantis::Result<std::vector<VertexTangent>, CookError> generateTangentsU32(
    const std::vector<MeshSourceVertex>& vertices, const std::vector<std::uint32_t>& indices,
    TangentGenerationStats* stats = nullptr);

}  // namespace atlantis::asset_system

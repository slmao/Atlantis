#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace atlantis::asset_system {

namespace {

// All comparisons operate on double, matching ADR-0063's own numeric-
// contract precision discipline (ADR-0073 Decision item 3).
struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] Vec3 scale(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] double length(const Vec3& a) { return std::sqrt(dot(a, a)); }

[[nodiscard]] Vec3 positionOf(const MeshSourceVertex& v) {
  return {static_cast<double>(v.positionX), static_cast<double>(v.positionY), static_cast<double>(v.positionZ)};
}
[[nodiscard]] Vec3 normalOf(const MeshSourceVertex& v) {
  return {static_cast<double>(v.normalX), static_cast<double>(v.normalY), static_cast<double>(v.normalZ)};
}

// ADR-0073 Decision item 3's own fixed epsilons.
constexpr double kUvDegeneracyEpsilon = 1e-12;
constexpr double kOrthogonalizationDegeneracyEpsilon = 1e-6;  // kDegenerateLengthEpsilon
constexpr double kHandednessZeroTiebreakEpsilon = 1e-9;
// ADR-0073's own Accepted Correction, 2026-09-06: geometric-degeneracy
// threshold, checked before the UV-degeneracy check above.
constexpr double kGeometricDegeneracyRatioEpsilon = 1e-12;

[[nodiscard]] double signWithTiebreak(double value) {
  if (std::abs(value) < kHandednessZeroTiebreakEpsilon) return 1.0;
  return value > 0.0 ? 1.0 : -1.0;
}

const std::array<Vec3, 3> kFallbackAxes = {Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 1.0}};

struct VertexAccumulator {
  Vec3 tangentSum{};
  bool hasContribution = false;
  bool hasConflict = false;
  std::optional<double> firstHandedness;
};

}  // namespace

atlantis::Result<std::vector<VertexTangent>, CookError> generateTangents(const ParsedMeshSource& source) {
  using ResultT = atlantis::Result<std::vector<VertexTangent>, CookError>;

  std::vector<VertexAccumulator> accumulators(source.vertices.size());

  for (std::size_t i = 0; i + 2 < source.indices.size(); i += 3) {
    const std::uint16_t v0 = source.indices[i];
    const std::uint16_t v1 = source.indices[i + 1];
    const std::uint16_t v2 = source.indices[i + 2];

    const Vec3 p0 = positionOf(source.vertices[v0]);
    const Vec3 p1 = positionOf(source.vertices[v1]);
    const Vec3 p2 = positionOf(source.vertices[v2]);

    const Vec3 e1 = sub(p1, p0);
    const Vec3 e2 = sub(p2, p0);
    const Vec3 e3 = sub(p2, p1);

    // ADR-0073's own Accepted Correction, 2026-09-06: geometric
    // degeneracy is checked before UV degeneracy -- a triangle whose
    // real 3D area is zero or near-zero (e.g. pbr_sphere's own
    // pole-closing triangles, which connect two duplicate pole-point
    // vertices) contributes nothing, regardless of its own UV-space
    // determinant, which can be large.
    const double edgeScale = std::max({length(e1), length(e2), length(e3)});
    if (edgeScale == 0.0) continue;
    const double area2 = length(cross(e1, e2));
    const double geometricRatio = area2 / (edgeScale * edgeScale);
    if (geometricRatio < kGeometricDegeneracyRatioEpsilon) continue;

    const double d1u = static_cast<double>(source.vertices[v1].uvU) - static_cast<double>(source.vertices[v0].uvU);
    const double d1v = static_cast<double>(source.vertices[v1].uvV) - static_cast<double>(source.vertices[v0].uvV);
    const double d2u = static_cast<double>(source.vertices[v2].uvU) - static_cast<double>(source.vertices[v0].uvU);
    const double d2v = static_cast<double>(source.vertices[v2].uvV) - static_cast<double>(source.vertices[v0].uvV);

    const double det = d1u * d2v - d2u * d1v;
    if (std::abs(det) < kUvDegeneracyEpsilon) continue;

    const Vec3 tFace = scale(sub(scale(e1, d2v), scale(e2, d1v)), 1.0 / det);
    const Vec3 bFace = scale(sub(scale(e2, d1u), scale(e1, d2u)), 1.0 / det);

    const std::array<std::uint16_t, 3> corners = {v0, v1, v2};
    for (std::uint16_t vertexIndex : corners) {
      VertexAccumulator& accumulator = accumulators[vertexIndex];
      const Vec3 normal = normalOf(source.vertices[vertexIndex]);
      const double hFace = signWithTiebreak(dot(cross(normal, tFace), bFace));

      if (!accumulator.hasContribution) {
        accumulator.firstHandedness = hFace;
      } else if (hFace != *accumulator.firstHandedness) {
        accumulator.hasConflict = true;
      }
      accumulator.tangentSum = add(accumulator.tangentSum, tFace);
      accumulator.hasContribution = true;
    }
  }

  // ADR-0073 Decision item 4's own mandatory, whole-mesh handedness-
  // conflict check: every vertex is checked before any final tangent
  // is computed for any vertex -- no partial artifact is ever written.
  for (const VertexAccumulator& accumulator : accumulators) {
    if (accumulator.hasConflict) return ResultT::Err(CookError::TangentHandednessConflict);
  }

  std::vector<VertexTangent> tangents(source.vertices.size());
  for (std::size_t vertexIndex = 0; vertexIndex < source.vertices.size(); ++vertexIndex) {
    const VertexAccumulator& accumulator = accumulators[vertexIndex];
    const Vec3 normal = normalOf(source.vertices[vertexIndex]);

    if (!accumulator.hasContribution) {
      // ADR-0073 Decision item 4a: deterministic fallback for a vertex
      // referenced by zero non-degenerate triangles (minimal_cube's own
      // real case, and pbr_sphere's own two seam-closure duplicates
      // under the corrected geometric-degeneracy check).
      std::size_t bestAxis = 0;
      double bestAbsDot = std::abs(dot(normal, kFallbackAxes[0]));
      for (std::size_t axis = 1; axis < kFallbackAxes.size(); ++axis) {
        const double absDot = std::abs(dot(normal, kFallbackAxes[axis]));
        if (absDot < bestAbsDot) {
          bestAbsDot = absDot;
          bestAxis = axis;
        }
      }
      const Vec3 tRaw = sub(kFallbackAxes[bestAxis], scale(normal, dot(normal, kFallbackAxes[bestAxis])));
      const Vec3 tUnit = scale(tRaw, 1.0 / length(tRaw));
      tangents[vertexIndex] = VertexTangent{static_cast<float>(tUnit.x), static_cast<float>(tUnit.y),
                                             static_cast<float>(tUnit.z), 1.0f};
      continue;
    }

    const Vec3 orthoRaw = sub(accumulator.tangentSum, scale(normal, dot(normal, accumulator.tangentSum)));
    const double orthoLength = length(orthoRaw);
    if (orthoLength < kOrthogonalizationDegeneracyEpsilon) return ResultT::Err(CookError::DegenerateTangentBasis);

    const Vec3 orthoUnit = scale(orthoRaw, 1.0 / orthoLength);
    const double handedness = *accumulator.firstHandedness;
    tangents[vertexIndex] = VertexTangent{static_cast<float>(orthoUnit.x), static_cast<float>(orthoUnit.y),
                                           static_cast<float>(orthoUnit.z), static_cast<float>(handedness)};
  }

  return ResultT::Ok(std::move(tangents));
}

}  // namespace atlantis::asset_system

#include "scene_picking.h"

#include <atlantis/assert.h>
#include <atlantis/schema.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
#include <variant>

namespace atlantis::runtime::detail {

namespace access = atlantis::world::access;
using atlantis::asset_system::AssetId;
using atlantis::asset_system::EntityGuid;
using Float3 = std::array<float, 3>;

namespace {

// The World schema's names (world_schema.cpp); ids derive from them.
constexpr std::string_view kRenderable = "world::Renderable";
constexpr std::string_view kWorldMatrix = "world::WorldMatrix";

template <typename Index>
[[nodiscard]] PickMeshGeometry build(const atlantis::rhi::VertexInputLayout& layout,
                                     const std::vector<std::byte>& vertexBytes, std::span<const Index> indices) {
  PickMeshGeometry geometry;
  const auto position = std::find_if(layout.attributes.begin(), layout.attributes.end(),
                                     [](const atlantis::rhi::VertexAttribute& a) { return a.location == 0; });
  ATLANTIS_CHECK_MSG(layout.strideBytes > 0 && vertexBytes.size() % layout.strideBytes == 0 &&
                         position != layout.attributes.end() &&
                         position->format == atlantis::rhi::VertexAttributeFormat::Float3 &&
                         position->offsetBytes + 3 * sizeof(float) <= layout.strideBytes,
                     "buildPickGeometry(): the vertex layout must place a Float3 position at location 0");
  if (layout.strideBytes == 0 || position == layout.attributes.end()) return geometry;
  const std::size_t vertexCount = vertexBytes.size() / layout.strideBytes;
  geometry.positions.resize(vertexCount);
  Float3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max()};
  Float3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest()};
  for (std::size_t v = 0; v < vertexCount; ++v) {
    Float3 xyz;
    std::memcpy(xyz.data(), vertexBytes.data() + v * layout.strideBytes + position->offsetBytes, sizeof(xyz));
    geometry.positions[v] = xyz;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      minimum[axis] = std::min(minimum[axis], xyz[axis]);
      maximum[axis] = std::max(maximum[axis], xyz[axis]);
    }
  }
  if (vertexCount > 0) {
    geometry.localMin = minimum;
    geometry.localMax = maximum;
  }
  geometry.indices.assign(indices.begin(), indices.end());
  return geometry;
}

[[nodiscard]] Float3 transformPoint(const std::array<float, 16>& m, const Float3& p) {
  return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
          m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

// The ray's parameter interval inside the box, clipped to [0, limit]; empty
// when it misses.
[[nodiscard]] std::optional<float> slabEntry(const PickRay& ray, const Float3& lo, const Float3& hi, float limit) {
  float tNear = 0.0f;
  float tFar = limit;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const float d = ray.direction[axis];
    if (std::fabs(d) < 1e-20f) {
      if (ray.origin[axis] < lo[axis] || ray.origin[axis] > hi[axis]) return std::nullopt;
      continue;
    }
    float t0 = (lo[axis] - ray.origin[axis]) / d;
    float t1 = (hi[axis] - ray.origin[axis]) / d;
    if (t0 > t1) std::swap(t0, t1);
    tNear = std::max(tNear, t0);
    tFar = std::min(tFar, t1);
    if (tNear > tFar) return std::nullopt;
  }
  return tNear;
}

// Moller-Trumbore, both faces: the hit's distance along the ray, if any.
[[nodiscard]] std::optional<float> intersect(const PickRay& ray, const Float3& a, const Float3& b, const Float3& c) {
  const Float3 e1{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const Float3 e2{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  const Float3& d = ray.direction;
  const Float3 p{d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
  const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
  if (std::fabs(det) < 1e-12f) return std::nullopt;
  const float inv = 1.0f / det;
  const Float3 s{ray.origin[0] - a[0], ray.origin[1] - a[1], ray.origin[2] - a[2]};
  const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
  if (u < 0.0f || u > 1.0f) return std::nullopt;
  const Float3 q{s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
  const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
  if (v < 0.0f || u + v > 1.0f) return std::nullopt;
  const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
  if (!(t > 0.0f)) return std::nullopt;
  return t;
}

}  // namespace

PickMeshGeometry buildPickGeometry(const atlantis::rhi::VertexInputLayout& layout,
                                   const std::vector<std::byte>& vertexBytes, std::span<const std::uint16_t> indices) {
  return build(layout, vertexBytes, indices);
}

PickMeshGeometry buildPickGeometry(const atlantis::rhi::VertexInputLayout& layout,
                                   const std::vector<std::byte>& vertexBytes, std::span<const std::uint32_t> indices) {
  return build(layout, vertexBytes, indices);
}

std::optional<AssetId> renderableMesh(const access::RuntimeWorldAccess& boundary, const EntityGuid& entity) {
  const auto mesh = boundary.getProperty(
      access::PropertyAddress{entity, schema::typeId(kRenderable), schema::fieldId(kRenderable, "meshAsset")});
  if (mesh.isErr()) return std::nullopt;
  const auto* id = std::get_if<std::uint64_t>(&mesh.value());
  if (id == nullptr) return std::nullopt;
  return AssetId{*id};
}

std::optional<AssetId> renderableMaterial(const access::RuntimeWorldAccess& boundary, const EntityGuid& entity) {
  const auto material = boundary.getProperty(
      access::PropertyAddress{entity, schema::typeId(kRenderable), schema::fieldId(kRenderable, "materialAsset")});
  if (material.isErr()) return std::nullopt;
  const auto* id = std::get_if<std::uint64_t>(&material.value());  // Absent when the Renderable has none
  if (id == nullptr) return std::nullopt;
  return AssetId{*id};
}

std::vector<PickInstance> collectPickInstances(const access::RuntimeWorldAccess& boundary,
                                               const atlantis::world::BakedScene& scene) {
  const schema::TypeId worldMatrixType = schema::typeId(kWorldMatrix);
  const std::array<schema::FieldId, 4> columns{
      schema::fieldId(kWorldMatrix, "column0"), schema::fieldId(kWorldMatrix, "column1"),
      schema::fieldId(kWorldMatrix, "column2"), schema::fieldId(kWorldMatrix, "column3")};
  // The bake's GUIDs carry their node order (EntityId::index(), as the
  // frame's collect*() order them); entities created since come after.
  std::vector<std::pair<std::uint64_t, PickInstance>> ordered;
  const auto bakedEntries = scene.entities.entries();
  std::uint64_t created = 0;
  for (const EntityGuid& entity : boundary.listEntities()) {
    const std::optional<AssetId> mesh = renderableMesh(boundary, entity);
    if (!mesh.has_value()) continue;
    PickInstance instance{entity, *mesh, {}};
    bool complete = true;
    for (std::size_t k = 0; k < 4 && complete; ++k) {
      const auto value = boundary.getProperty(access::PropertyAddress{entity, worldMatrixType, columns[k]});
      const auto* column = value.isOk() ? std::get_if<std::array<float, 4>>(&value.value()) : nullptr;
      if (column == nullptr) {
        complete = false;
      } else {
        std::memcpy(instance.worldMatrix.data() + k * 4, column->data(), sizeof(float) * 4);
      }
    }
    if (!complete) continue;
    // entries() is sorted by GUID, as listEntities() is.
    const auto baked = std::lower_bound(bakedEntries.begin(), bakedEntries.end(), entity,
                                        [](const auto& entry, const EntityGuid& guid) { return entry.first < guid; });
    const std::uint64_t key = baked != bakedEntries.end() && baked->first == entity
                                  ? baked->second.index()
                                  : (std::uint64_t{1} << 32) + created++;
    ordered.emplace_back(key, instance);
  }
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<PickInstance> instances;
  instances.reserve(ordered.size());
  for (auto& [key, instance] : ordered) instances.push_back(instance);
  return instances;
}

namespace {
template <bool Bounded = false, typename Limit, typename Visit>
bool visitHits(std::span<const PickInstance> instances,
               const std::unordered_map<AssetId, PickMeshGeometry>& geometry, const PickRay& ray,
               Limit&& limit, Visit&& visit) {
  // Each instance's world bounds first; candidates nearest-entry first, so the
  // search stops once no remaining box can hold a nearer hit.
  struct Candidate {
    float entry;
    std::size_t index;
    const PickMeshGeometry* mesh;
  };
  std::vector<Candidate> candidates;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    const PickInstance& instance = instances[i];
    const auto it = geometry.find(instance.mesh);
    if (it == geometry.end() || it->second.indices.empty()) continue;
    if (!std::all_of(instance.worldMatrix.begin(), instance.worldMatrix.end(),
                     [](float v) { return std::isfinite(v); })) {
      continue;
    }
    const PickMeshGeometry& mesh = it->second;
    Float3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Float3 hi{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest()};
    for (int corner = 0; corner < 8; ++corner) {
      const Float3 local{(corner & 1) ? mesh.localMax[0] : mesh.localMin[0],
                         (corner & 2) ? mesh.localMax[1] : mesh.localMin[1],
                         (corner & 4) ? mesh.localMax[2] : mesh.localMin[2]};
      const Float3 world = transformPoint(instance.worldMatrix, local);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        lo[axis] = std::min(lo[axis], world[axis]);
        hi[axis] = std::max(hi[axis], world[axis]);
      }
    }
    if (const auto entry = slabEntry(ray, lo, hi, std::numeric_limits<float>::max()); entry.has_value()) {
      candidates.push_back(Candidate{*entry, i, &mesh});
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& a, const Candidate& b) { return a.entry < b.entry; });

  std::vector<Float3> world;
  [[maybe_unused]] std::size_t triangles = 0;
  for (const Candidate& candidate : candidates) {
    if (candidate.entry > limit()) break;
    const PickInstance& instance = instances[candidate.index];
    const PickMeshGeometry& mesh = *candidate.mesh;
    if constexpr (Bounded) {
      const std::size_t count = mesh.indices.size() / 3;
      if (count > atlantis::connection::kMaterialBindingTriangles - triangles) return false;
      triangles += count;
    } else {
      world.resize(mesh.positions.size());
      for (std::size_t v = 0; v < mesh.positions.size(); ++v)
        world[v] = transformPoint(instance.worldMatrix, mesh.positions[v]);
    }
    const auto vertex = [&](std::uint32_t index) {
      if constexpr (Bounded) return transformPoint(instance.worldMatrix, mesh.positions[index]);
      else return world[index];
    };
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
      const std::uint32_t i0 = mesh.indices[t];
      const std::uint32_t i1 = mesh.indices[t + 1];
      const std::uint32_t i2 = mesh.indices[t + 2];
      if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size()) {
        if constexpr (Bounded) return false;
        continue;
      }
      const std::optional<float> hit = intersect(ray, vertex(i0), vertex(i1), vertex(i2));
      if (!hit.has_value()) continue;
      visit(candidate.index, t/3, *hit);
    }
  }
  return true;
}
}

MaterialSurfaceRead pickMaterialSurface(
    std::span<const PickInstance> instances,
    const std::unordered_map<AssetId, PickMeshGeometry>& geometry,
    const atlantis::connection::MaterialBindingPickRequest& request) {
  using R = MaterialSurfaceRead;
  using E = atlantis::connection::MaterialBindingPickError;
  if (!atlantis::connection::validMaterialBindingPick(request)) return R::Err(E::InvalidRequest);
  if (instances.size() > atlantis::connection::kMaterialBindingInstances) return R::Err(E::TooLarge);
  for (const auto& instance : instances) {
    const auto mesh = geometry.find(instance.mesh);
    if (mesh == geometry.end() || mesh->second.indices.empty() || mesh->second.indices.size() % 3 != 0 ||
        !std::ranges::all_of(instance.worldMatrix, [](float value) { return std::isfinite(value); }))
      return R::Err(E::NoGeometry);
  }
  const PickRay ray{request.ray.origin, request.ray.direction};
  const auto epsilon = [](float distance) { return 1e-5 * std::max(1.0, static_cast<double>(distance)); };
  std::size_t first = instances.size();
  float nearest = request.maxDistance;
  float next = std::numeric_limits<float>::infinity();
  const auto completed = visitHits<true>(instances, geometry, ray,
      [&] { return std::min<double>(request.maxDistance, nearest + epsilon(nearest)); },
      [&](std::size_t index, std::size_t, float distance) {
        if (distance < request.minDistance || distance > request.maxDistance) return;
        if (distance <= nearest) {
          if (index != first && first != instances.size()) next = nearest;
          nearest = distance;
          first = index;
        } else if (index != first) next = std::min(next, distance);
      });
  if (!completed) return R::Err(E::TooLarge);
  if (first == instances.size()) return R::Err(E::Miss);
  if (static_cast<double>(next) <= nearest + epsilon(nearest)) return R::Err(E::Ambiguous);
  return R::Ok({instances[first].entity, nearest});
}

std::optional<PickHit> pickNearest(std::span<const PickInstance> instances,
                                   const std::unordered_map<AssetId, PickMeshGeometry>& geometry, const PickRay& ray) {
  std::optional<PickHit> best;
  std::size_t bestIndex = 0;
  visitHits(instances, geometry, ray,
      [&] { return best ? best->distance : std::numeric_limits<float>::max(); },
      [&](std::size_t index, std::size_t, float hit) {
        if (!best || hit < best->distance || (hit == best->distance && index < bestIndex)) {
          best = PickHit{instances[index].entity, hit};
          bestIndex = index;
        }
      });
  return best;
}

namespace {
using Point = std::array<double, 3>;
using Face = connection::AlignmentFace;
using Error = connection::AlignmentFaceError;
using Triangle = std::array<Float3, 3>;

Point subtract(const Point& a, const Point& b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
Point widen(const Float3& p) { return {p[0], p[1], p[2]}; }
double dot(const Point& a, const Point& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Point cross(const Point& a, const Point& b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
double length(const Point& p) { return std::sqrt(dot(p, p)); }
Point unit(const Point& p) {
  const double size = length(p);
  return size > 0 && std::isfinite(size) ? Point{p[0]/size,p[1]/size,p[2]/size} : Point{};
}
Point worldPoint(const std::array<float,16>& m, const Float3& p) {
  Point result{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    result[axis] = m[axis+12];
    for (std::size_t col = 0; col < 3; ++col) result[axis] += static_cast<double>(m[axis+4*col]) * p[col];
  }
  return result;
}
bool supportedBasis(const std::array<float,16>& m) {
  if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1) return false;
  const Point x = unit({m[0],m[1],m[2]}), y = unit({m[4],m[5],m[6]}), z = unit({m[8],m[9],m[10]});
  return std::abs(dot(x,y)) <= 1e-4 && std::abs(dot(y,z)) <= 1e-4 && std::abs(dot(z,x)) <= 1e-4 &&
         dot(cross(x,y),z) > 0.9999;
}
std::optional<Triangle> triangle(const PickMeshGeometry& mesh, std::size_t index) {
  Triangle result{};
  for (std::size_t j = 0; j < 3; ++j) {
    const auto vertex = mesh.indices[3*index+j];
    if (vertex >= mesh.positions.size()) return {};
    result[j] = mesh.positions[vertex];
    for (auto& component : result[j]) {
      if (!std::isfinite(component)) return {};
      if (component == 0) component = 0;
    }
  }
  return result;
}

// The second triangle must traverse the same diagonal in reverse. Reading
// the other two edges of each triangle yields the cyclic perimeter.
std::optional<std::array<Float3, 4>> perimeter(const Triangle& a, const Triangle& b) {
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      if (a[i] != b[(j+1)%3] || a[(i+1)%3] != b[j]) continue;
      std::array<Float3, 4> ring{a[i], b[(j+2)%3], a[(i+1)%3], a[(i+2)%3]};
      const auto first = std::min_element(ring.begin(), ring.end());
      if (std::set<Float3>(ring.begin(), ring.end()).size() != ring.size()) return {};
      std::rotate(ring.begin(), first, ring.end());
      return ring;
    }
  }
  return {};
}

std::optional<double> rectangle(const std::array<Point,4>& points, const Point& normal) {
  constexpr double sinAngle = 0.0017453283658983088; // sin(0.1 degrees)
  std::array<Point,4> edges{};
  double longest = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    edges[i] = subtract(points[(i+1)%4], points[i]);
    longest = std::max(longest, length(edges[i]));
  }
  const double epsilon = std::max(1e-5, 1e-5 * longest);
  if (!std::isfinite(longest)) return {};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& edge = edges[i];
    const auto& next = edges[(i+1)%4];
    const auto& opposite = edges[(i+2)%4];
    if (length(edge) < std::max(1e-3,100*epsilon) ||
        std::abs(dot(unit(edge),unit(next))) > sinAngle ||
        dot(cross(unit(edge),unit(next)), normal) <= 0 ||
        std::abs(dot(subtract(points[i],points[0]),normal)) > epsilon ||
        length({edge[0]+opposite[0],edge[1]+opposite[1],edge[2]+opposite[2]}) > epsilon) return {};
  }
  return epsilon;
}

bool interior(const std::array<Point,4>& ring, const Point& normal, const connection::Ray& ray, double t, double epsilon) {
  const auto origin = widen(ray.origin), dir = widen(ray.direction);
  const Point hit{origin[0]+dir[0]*t, origin[1]+dir[1]*t, origin[2]+dir[2]*t};
  if (!(std::abs(dot(subtract(hit, ring[0]), normal)) <= epsilon)) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto edge = unit(subtract(ring[(i+1)%4],ring[i]));
    if (!(dot(cross(edge,subtract(hit,ring[i])),normal) > epsilon)) return false;
  }
  return true;
}
}

AlignmentFaceRead pickAlignmentFace(std::span<const PickInstance> instances,
    const std::unordered_map<AssetId, PickMeshGeometry>& geometry, const connection::Ray& ray) {
  using R = AlignmentFaceRead;
  if (!connection::validAlignmentRay(ray)) return R::Err(Error::InvalidRequest);
  const PickRay pick{ray.origin,ray.direction};
  std::size_t firstInstance = instances.size(), firstTriangle = 0;
  float distance = std::numeric_limits<float>::max();
  visitHits(instances, geometry, pick, [&] { return distance; },
      [&](std::size_t index, std::size_t tri, float hit) {
        if (hit < distance || (hit == distance && index < firstInstance)) {
          firstInstance = index;
          firstTriangle = tri;
          distance = hit;
        }
      });
  if (firstInstance == instances.size()) return R::Err(Error::Miss);
  const auto& instance = instances[firstInstance];
  const auto& mesh = geometry.at(instance.mesh);
  const std::size_t count = mesh.indices.size()/3;
  if (count > connection::kAlignmentTriangleLimit) return R::Err(Error::TooLarge);
  if (mesh.indices.size()%3 != 0 || !instance.opaqueForAlignment || mesh.assetGuid == asset_system::AssetGuid{} ||
      !supportedBasis(instance.worldMatrix)) return R::Err(Error::UnsupportedFace);

  const auto point = [&](const Float3& local) { return worldPoint(instance.worldMatrix, local); };
  const auto normalOf = [&](const Triangle& tri) {
    const auto a = point(tri[0]);
    return unit(cross(subtract(point(tri[1]),a),subtract(point(tri[2]),a)));
  };
  const auto picked = triangle(mesh, firstTriangle);
  if (!picked || dot(normalOf(*picked),widen(ray.direction)) >= -1e-6) return R::Err(Error::UnsupportedFace);

  Face face{instance.entity, mesh.assetGuid, {}, distance, {}, {}, instance.worldMatrix};
  std::array<Point,4> world{};
  Point normal{};
  std::optional<std::size_t> mate;
  double tolerance = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (i == firstTriangle) continue;
    const auto other = triangle(mesh,i);
    if (!other) return R::Err(Error::UnsupportedFace);
    const auto ring = perimeter(*picked,*other);
    if (!ring) continue;
    const auto outward = normalOf(i < firstTriangle ? *other : *picked);
    if (dot(outward, normalOf(*other)) < 0.9999984769132877 ||
        dot(outward, normalOf(*picked)) < 0.9999984769132877 || dot(outward, widen(ray.direction)) >= -1e-6) continue;
    std::array<Point,4> candidate{};
    std::ranges::transform(*ring, candidate.begin(), point);
    const auto epsilon = rectangle(candidate,outward);
    if (!epsilon) continue;
    if (mate) return R::Err(Error::Ambiguous);
    mate = i;
    world = candidate;
    tolerance = *epsilon;
    normal = outward;
  }
  if (!mate) return R::Err(Error::UnsupportedFace);
  if (!interior(world, normal, ray, distance, tolerance)) return R::Err(Error::Ambiguous);

  for (std::size_t i = 0; i < count; ++i) {
    if (i == firstTriangle || i == *mate) continue;
    const auto other = triangle(mesh,i); // first scan checked every index
    if (!other) return R::Err(Error::UnsupportedFace);
    if (length(normalOf(*other)) != 0 && std::ranges::all_of(*other, [&](const Float3& p) {
          return std::abs(dot(subtract(point(p),world[0]),normal)) <= tolerance;
        })) return R::Err(Error::Ambiguous);
  }

  bool ambiguous = false;
  const double limit = static_cast<double>(distance) + tolerance;
  visitHits(instances, geometry, pick, [&] { return limit; },
      [&](std::size_t index, std::size_t tri, float hit) {
        if (static_cast<double>(hit) <= limit && (index != firstInstance || (tri != firstTriangle && tri != *mate)))
          ambiguous = true;
      });
  if (ambiguous) return R::Err(Error::Ambiguous);

  face.triangles = {static_cast<std::uint32_t>(std::min(firstTriangle,*mate)),
                    static_cast<std::uint32_t>(std::max(firstTriangle,*mate))};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    face.normal[axis] = static_cast<float>(normal[axis]);
    for (std::size_t i = 0; i < 4; ++i) face.corners[i][axis] = static_cast<float>(world[i][axis]);
  }
  std::array<Point,4> wire{};
  std::ranges::transform(face.corners, wire.begin(), widen);
  const auto epsilon = rectangle(wire,widen(face.normal));
  if (!epsilon || !interior(wire,widen(face.normal),ray,distance,*epsilon)) return R::Err(Error::UnsupportedFace);
  return R::Ok(std::move(face));
}

}  // namespace atlantis::runtime::detail

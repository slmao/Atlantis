#include "scene_picking.h"

#include <atlantis/assert.h>
#include <atlantis/schema.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

std::optional<PickHit> pickNearest(std::span<const PickInstance> instances,
                                   const std::unordered_map<AssetId, PickMeshGeometry>& geometry, const PickRay& ray) {
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

  std::optional<PickHit> best;
  std::size_t bestIndex = 0;
  std::vector<Float3> world;
  for (const Candidate& candidate : candidates) {
    if (best.has_value() && candidate.entry > best->distance) break;
    const PickInstance& instance = instances[candidate.index];
    const PickMeshGeometry& mesh = *candidate.mesh;
    world.resize(mesh.positions.size());
    for (std::size_t v = 0; v < mesh.positions.size(); ++v) world[v] = transformPoint(instance.worldMatrix, mesh.positions[v]);
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
      const std::uint32_t i0 = mesh.indices[t];
      const std::uint32_t i1 = mesh.indices[t + 1];
      const std::uint32_t i2 = mesh.indices[t + 2];
      if (i0 >= world.size() || i1 >= world.size() || i2 >= world.size()) continue;
      const std::optional<float> hit = intersect(ray, world[i0], world[i1], world[i2]);
      if (!hit.has_value()) continue;
      if (!best.has_value() || *hit < best->distance ||
          (*hit == best->distance && candidate.index < bestIndex)) {
        best = PickHit{instance.entity, *hit};
        bestIndex = candidate.index;
      }
    }
  }
  return best;
}

}  // namespace atlantis::runtime::detail

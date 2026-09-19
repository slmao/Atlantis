#define CGLTF_IMPLEMENTATION
#include "import_command.h"

#include "material_import.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <variant>

// Plan 0037 Milestone 3: glTF mesh primitives -> .amesh schema 5. The one
// cgltf implementation translation unit in this target (the same
// single-TU discipline atlantis_asset_cooker applies to stb_image). D6: no
// coordinate conversion (identical conventions, ADR-0050). D7: COLOR_0
// absent -> white (Bistro has none); present -> RGB read, alpha dropped and
// reported. D8: upstream TANGENT read only for presence reporting, then
// discarded in favour of generateTangentsU32().

namespace atlantis::gltf_importer {

namespace {

namespace fs = std::filesystem;
using atlantis::asset_system::MeshSourceVertex;

struct CgltfDataGuard {
  cgltf_data* data = nullptr;
  ~CgltfDataGuard() {
    if (data != nullptr) cgltf_free(data);
  }
};

// Removes the staging directory on every exit path except a committed one.
struct StagingGuard {
  fs::path path;
  bool committed = false;
  ~StagingGuard() {
    if (!committed && !path.empty()) {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  }
};

// ADR-0063's own numeric-contract tolerance, reused verbatim via the
// canonical Asset System helpers (never a restated constant -- the
// artifact decoder enforces the same pair independently).
[[nodiscard]] bool isNormalInTolerance(float x, float y, float z) {
  return atlantis::asset_system::detail::isNormalLengthSquaredInTolerance(
      atlantis::asset_system::detail::computeNormalLengthSquared(x, y, z));
}

[[nodiscard]] bool writeBytes(const fs::path& path, const char* data, std::size_t size) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(data, static_cast<std::streamsize>(size));
  out.flush();
  return out.good();
}

struct PrimitiveAccessors {
  const cgltf_accessor* position = nullptr;
  const cgltf_accessor* normal = nullptr;
  const cgltf_accessor* texcoord = nullptr;
  const cgltf_accessor* color = nullptr;
  const cgltf_accessor* tangent = nullptr;
};

[[nodiscard]] PrimitiveAccessors attributesOf(const cgltf_primitive& primitive) {
  PrimitiveAccessors result;
  for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
    const cgltf_attribute& attribute = primitive.attributes[i];
    switch (attribute.type) {
      case cgltf_attribute_type_position:
        result.position = attribute.data;
        break;
      case cgltf_attribute_type_normal:
        result.normal = attribute.data;
        break;
      case cgltf_attribute_type_texcoord:
        if (attribute.index == 0) result.texcoord = attribute.data;
        break;
      case cgltf_attribute_type_color:
        if (attribute.index == 0) result.color = attribute.data;
        break;
      case cgltf_attribute_type_tangent:
        result.tangent = attribute.data;
        break;
      default:
        break;
    }
  }
  return result;
}

// An accessor's bytes must lie inside its bufferView, and the bufferView
// inside its buffer. Checked by the importer itself (rather than left to
// cgltf_validate's coarse data_too_short) so the failure is a named error.
[[nodiscard]] bool isAccessorInRange(const cgltf_accessor& accessor) {
  if (accessor.is_sparse) return false;
  const cgltf_buffer_view* view = accessor.buffer_view;
  if (view == nullptr || view->buffer == nullptr) return false;
  if (view->offset > view->buffer->size || view->size > view->buffer->size - view->offset) return false;
  if (accessor.count == 0) return true;
  const cgltf_size elementSize = cgltf_calc_size(accessor.type, accessor.component_type);
  const cgltf_size stride = accessor.stride != 0 ? accessor.stride : elementSize;
  if (elementSize == 0 || accessor.offset > view->size) return false;
  const cgltf_size available = view->size - accessor.offset;
  if (elementSize > available) return false;
  return (accessor.count - 1) <= (available - elementSize) / stride;
}

// Every per-primitive structural check, run over the whole file before any
// output is produced and before cgltf_validate (whose failures would
// otherwise all collapse into MalformedGltf).
[[nodiscard]] atlantis::Result<std::monostate, GltfImportError> checkPrimitiveStructure(
    const cgltf_primitive& primitive) {
  using ResultT = atlantis::Result<std::monostate, GltfImportError>;
  if (primitive.type != cgltf_primitive_type_triangles) return ResultT::Err(GltfImportError::NonTrianglesMode);
  if (primitive.indices == nullptr) return ResultT::Err(GltfImportError::NonIndexedPrimitive);
  const PrimitiveAccessors accessors = attributesOf(primitive);
  if (accessors.position == nullptr) return ResultT::Err(GltfImportError::MissingPosition);
  if (accessors.normal == nullptr || accessors.texcoord == nullptr) {
    return ResultT::Err(GltfImportError::MissingRequiredAttribute);
  }
  const cgltf_component_type indexType = primitive.indices->component_type;
  if (primitive.indices->type != cgltf_type_scalar ||
      (indexType != cgltf_component_type_r_8u && indexType != cgltf_component_type_r_16u &&
       indexType != cgltf_component_type_r_32u)) {
    return ResultT::Err(GltfImportError::UnsupportedIndexComponentType);
  }
  for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
    if (!isAccessorInRange(*primitive.attributes[i].data)) return ResultT::Err(GltfImportError::OutOfRangeAccessor);
  }
  if (!isAccessorInRange(*primitive.indices)) return ResultT::Err(GltfImportError::OutOfRangeAccessor);
  const cgltf_size vertexCount = accessors.position->count;
  for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
    if (primitive.attributes[i].data->count != vertexCount) {
      return ResultT::Err(GltfImportError::AttributeCountMismatch);
    }
  }
  for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
    if (cgltf_accessor_read_index(primitive.indices, i) >= vertexCount) {
      return ResultT::Err(GltfImportError::OutOfRangeIndex);
    }
  }
  return ResultT::Ok(std::monostate{});
}

// ---- Handedness split (human ruling 2026-09-19) ----------------------------
//
// ADR-0073's generator rejects a whole mesh when any vertex receives face
// tangent frames of both handedness signs -- standard in production assets
// that mirror or reuse UVs. The generator and its contract stay unchanged;
// instead, before calling it, the importer duplicates each conflicting
// vertex so every vertex sees one sign only: the first sign the generator
// would record keeps the original index, the other sign's faces are
// rewritten to a new, identical copy appended after all source vertices.
//
// The per-face sign below MUST be computed exactly as generateTangentsU32()'s
// accumulation stage computes it (src/asset_system/src/mesh_tangent_
// generation.cpp, ADR-0073 Decision item 3 and its 2026-09-06 Accepted
// Correction): same double-precision operations, same order, same epsilons.
// Any divergence surfaces as a TangentHandednessConflict from the generator
// call that follows, never as a silently wrong tangent.

struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] Vec3d scale(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3d cross(const Vec3d& a, const Vec3d& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] double length(const Vec3d& a) { return std::sqrt(dot(a, a)); }
[[nodiscard]] Vec3d positionOf(const MeshSourceVertex& v) {
  return {static_cast<double>(v.positionX), static_cast<double>(v.positionY), static_cast<double>(v.positionZ)};
}
[[nodiscard]] Vec3d normalOf(const MeshSourceVertex& v) {
  return {static_cast<double>(v.normalX), static_cast<double>(v.normalY), static_cast<double>(v.normalZ)};
}

constexpr double kUvDegeneracyEpsilon = 1e-12;
constexpr double kHandednessZeroTiebreakEpsilon = 1e-9;
constexpr double kGeometricDegeneracyRatioEpsilon = 1e-12;

[[nodiscard]] double signWithTiebreak(double value) {
  if (std::abs(value) < kHandednessZeroTiebreakEpsilon) return 1.0;
  return value > 0.0 ? 1.0 : -1.0;
}

// Returns the number of vertices appended. Deterministic: corners are
// visited in index-buffer order, duplicates appended in ascending source
// vertex order.
[[nodiscard]] std::size_t splitHandednessConflicts(std::vector<MeshSourceVertex>& vertices,
                                                   std::vector<std::uint32_t>& indices) {
  // Per corner: 0 = contributes nothing (degenerate face), otherwise +-1.
  std::vector<std::int8_t> cornerSign(indices.size(), 0);
  for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
    const std::uint32_t v0 = indices[i];
    const std::uint32_t v1 = indices[i + 1];
    const std::uint32_t v2 = indices[i + 2];

    const Vec3d p0 = positionOf(vertices[v0]);
    const Vec3d p1 = positionOf(vertices[v1]);
    const Vec3d p2 = positionOf(vertices[v2]);
    const Vec3d e1 = sub(p1, p0);
    const Vec3d e2 = sub(p2, p0);
    const Vec3d e3 = sub(p2, p1);

    const double edgeScale = std::max({length(e1), length(e2), length(e3)});
    if (edgeScale == 0.0) continue;
    const double area2 = length(cross(e1, e2));
    if (area2 / (edgeScale * edgeScale) < kGeometricDegeneracyRatioEpsilon) continue;

    const double d1u = static_cast<double>(vertices[v1].uvU) - static_cast<double>(vertices[v0].uvU);
    const double d1v = static_cast<double>(vertices[v1].uvV) - static_cast<double>(vertices[v0].uvV);
    const double d2u = static_cast<double>(vertices[v2].uvU) - static_cast<double>(vertices[v0].uvU);
    const double d2v = static_cast<double>(vertices[v2].uvV) - static_cast<double>(vertices[v0].uvV);
    const double det = d1u * d2v - d2u * d1v;
    if (std::abs(det) < kUvDegeneracyEpsilon) continue;

    const Vec3d tFace = scale(sub(scale(e1, d2v), scale(e2, d1v)), 1.0 / det);
    const Vec3d bFace = scale(sub(scale(e2, d1u), scale(e1, d2u)), 1.0 / det);
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const Vec3d normal = normalOf(vertices[indices[i + corner]]);
      cornerSign[i + corner] = signWithTiebreak(dot(cross(normal, tFace), bFace)) > 0.0 ? 1 : -1;
    }
  }

  const std::size_t sourceCount = vertices.size();
  std::vector<std::int8_t> firstSign(sourceCount, 0);
  std::vector<bool> conflicted(sourceCount, false);
  for (std::size_t c = 0; c < indices.size(); ++c) {
    if (cornerSign[c] == 0) continue;
    const std::uint32_t v = indices[c];
    if (firstSign[v] == 0) {
      firstSign[v] = cornerSign[c];
    } else if (firstSign[v] != cornerSign[c]) {
      conflicted[v] = true;
    }
  }

  std::vector<std::uint32_t> duplicateOf(sourceCount, 0);
  for (std::size_t v = 0; v < sourceCount; ++v) {
    if (!conflicted[v]) continue;
    duplicateOf[v] = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back(vertices[v]);
  }
  for (std::size_t c = 0; c < indices.size(); ++c) {
    const std::uint32_t v = indices[c];
    if (v < sourceCount && conflicted[v] && cornerSign[c] != 0 && cornerSign[c] != firstSign[v]) {
      indices[c] = duplicateOf[v];
    }
  }
  return vertices.size() - sourceCount;
}

[[nodiscard]] std::string formatPercent(double ratio) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.2f%%", ratio * 100.0);
  return buffer;
}

}  // namespace

const char* gltfImportErrorMessage(GltfImportError error) noexcept {
  switch (error) {
    case GltfImportError::InputUnreadable:
      return "input .gltf unreadable";
    case GltfImportError::MalformedGltf:
      return "malformed or invalid glTF (cgltf parse/validate failed)";
    case GltfImportError::BufferUnreadable:
      return "buffer URI unreadable";
    case GltfImportError::MissingTextureFile:
      return "a referenced texture file is missing under the content root";
    case GltfImportError::NonTrianglesMode:
      return "primitive mode is not TRIANGLES";
    case GltfImportError::NonIndexedPrimitive:
      return "primitive is not indexed";
    case GltfImportError::MissingPosition:
      return "primitive has no POSITION attribute";
    case GltfImportError::MissingRequiredAttribute:
      return "primitive has no NORMAL or no TEXCOORD_0 attribute";
    case GltfImportError::UnsupportedIndexComponentType:
      return "index accessor is not scalar u8/u16/u32";
    case GltfImportError::UnsupportedMaterialExtension:
      return "material uses an extension outside the supported set";
    case GltfImportError::UnsupportedTextureFeature:
      return "texture uses texCoord != 0, KHR_texture_transform, or one image as both colour and data";
    case GltfImportError::UnsupportedSamplerWrap:
      return "sampler uses MIRRORED_REPEAT or different wrapS/wrapT";
    case GltfImportError::TextureWithoutSource:
      return "texture has no usable MSFT_texture_dds or image source";
    case GltfImportError::OutOfRangeIndex:
      return "index value >= vertex count";
    case GltfImportError::OutOfRangeAccessor:
      return "accessor extends past its bufferView or buffer (or is sparse)";
    case GltfImportError::AttributeCountMismatch:
      return "primitive attribute accessors differ in vertex count";
    case GltfImportError::NonFiniteVertex:
      return "non-finite vertex float";
    case GltfImportError::NonUnitNormal:
      return "normal outside unit-length tolerance";
    case GltfImportError::InvalidMaterialFactor:
      return "material factor non-finite or outside [0, 1]";
    case GltfImportError::TangentGenerationFailed:
      return "tangent generation failed (handedness conflict left after the vertex split)";
    case GltfImportError::OutputDirectoryNotEmpty:
      return "output directory exists and is not empty";
    case GltfImportError::OutputWriteFailed:
      return "output write failed";
  }
  return "unknown gltf import error";
}

atlantis::Result<GltfImportSummary, GltfImportError> importGltf(const fs::path& inputPath, const fs::path& contentRoot,
                                                                 const fs::path& outputDir, const std::string& name) {
  using ResultT = atlantis::Result<GltfImportSummary, GltfImportError>;

  CgltfDataGuard guard;
  cgltf_options options{};
  const cgltf_result parseResult = cgltf_parse_file(&options, inputPath.string().c_str(), &guard.data);
  if (parseResult == cgltf_result_file_not_found || parseResult == cgltf_result_io_error) {
    return ResultT::Err(GltfImportError::InputUnreadable);
  }
  if (parseResult != cgltf_result_success || guard.data == nullptr) {
    return ResultT::Err(GltfImportError::MalformedGltf);
  }
  // cgltf concatenates relative buffer URIs onto this path verbatim --
  // it must end with a directory separator or the first path segment
  // merges ("content/bistro" + "bistro.bin").
  std::string contentDir = contentRoot.string();
  if (!contentDir.empty() && contentDir.back() != '/' && contentDir.back() != '\\') contentDir += '/';
  const cgltf_result bufferResult = cgltf_load_buffers(&options, guard.data, contentDir.c_str());
  if (bufferResult != cgltf_result_success) return ResultT::Err(GltfImportError::BufferUnreadable);

  for (cgltf_size m = 0; m < guard.data->meshes_count; ++m) {
    const cgltf_mesh& mesh = guard.data->meshes[m];
    for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
      const auto structure = checkPrimitiveStructure(mesh.primitives[p]);
      if (structure.isErr()) return ResultT::Err(structure.error());
    }
  }
  if (cgltf_validate(guard.data) != cgltf_result_success) return ResultT::Err(GltfImportError::MalformedGltf);
  const auto materialCheck = detail::checkMaterials(*guard.data, contentRoot);
  if (materialCheck.isErr()) return ResultT::Err(materialCheck.error());

  std::error_code ec;
  if (fs::exists(outputDir, ec) && !fs::is_empty(outputDir, ec)) {
    return ResultT::Err(GltfImportError::OutputDirectoryNotEmpty);
  }
  StagingGuard staging;
  staging.path = outputDir.parent_path() / (outputDir.filename().string() + ".importing");
  fs::remove_all(staging.path, ec);
  if (!fs::create_directories(staging.path, ec) || ec) return ResultT::Err(GltfImportError::OutputWriteFailed);

  GltfImportSummary summary;
  std::vector<std::string> reportLines;
  reportLines.push_back("gltf_import: " + name);

  for (cgltf_size meshIndex = 0; meshIndex < guard.data->meshes_count; ++meshIndex) {
    const cgltf_mesh& mesh = guard.data->meshes[meshIndex];
    for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
      const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
      const PrimitiveAccessors accessors = attributesOf(primitive);
      const cgltf_size vertexCount = accessors.position->count;
      const std::string base =
          name + "_mesh_" + std::to_string(meshIndex) + "_" + std::to_string(primitiveIndex);

      // Mesh names are not unique across the file (Bistro's three
      // >65535-vertex meshes are all named "subset_1"), so the logical
      // path is index-based -- always a valid logical path by construction.
      const std::string logicalPath = "meshes/" + name + "/mesh_" + std::to_string(meshIndex) + "_" +
                                      std::to_string(primitiveIndex);
      const auto normalizedResult = atlantis::asset_system::normalizeLogicalPath(logicalPath);
      if (normalizedResult.isErr()) return ResultT::Err(GltfImportError::OutputWriteFailed);
      const std::string& normalized = normalizedResult.value();
      const atlantis::asset_system::AssetId assetId = atlantis::asset_system::computeAssetId(normalized);

      if (accessors.tangent != nullptr) reportLines.push_back(base + ": upstream TANGENT discarded (D8)");

      std::vector<MeshSourceVertex> vertices;
      vertices.reserve(static_cast<std::size_t>(vertexCount));
      for (cgltf_size v = 0; v < vertexCount; ++v) {
        MeshSourceVertex vertex;
        float position[3];
        float normal[3];
        float uv[2];
        if (cgltf_accessor_read_float(accessors.position, v, position, 3) != 1 ||
            cgltf_accessor_read_float(accessors.normal, v, normal, 3) != 1 ||
            cgltf_accessor_read_float(accessors.texcoord, v, uv, 2) != 1) {
          return ResultT::Err(GltfImportError::OutOfRangeAccessor);
        }
        vertex.positionX = position[0];
        vertex.positionY = position[1];
        vertex.positionZ = position[2];
        vertex.normalX = normal[0];
        vertex.normalY = normal[1];
        vertex.normalZ = normal[2];
        vertex.uvU = uv[0];
        vertex.uvV = uv[1];

        // D7: absent COLOR_0 -> white; present -> RGB read, alpha dropped.
        if (accessors.color != nullptr) {
          float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
          if (cgltf_accessor_read_float(accessors.color, v, color, 4) != 1) {
            return ResultT::Err(GltfImportError::OutOfRangeAccessor);
          }
          vertex.colorR = color[0];
          vertex.colorG = color[1];
          vertex.colorB = color[2];
        } else {
          vertex.colorR = 1.0f;
          vertex.colorG = 1.0f;
          vertex.colorB = 1.0f;
        }

        const float floats[] = {vertex.positionX, vertex.positionY, vertex.positionZ, vertex.normalX,
                                vertex.normalY,   vertex.normalZ,   vertex.uvU,       vertex.uvV,
                                vertex.colorR,    vertex.colorG,    vertex.colorB};
        for (const float f : floats) {
          if (!std::isfinite(f)) return ResultT::Err(GltfImportError::NonFiniteVertex);
        }
        if (!isNormalInTolerance(vertex.normalX, vertex.normalY, vertex.normalZ)) {
          return ResultT::Err(GltfImportError::NonUnitNormal);
        }
        vertices.push_back(vertex);
      }

      const cgltf_size indexCount = primitive.indices->count;
      std::vector<std::uint32_t> indices;
      indices.reserve(static_cast<std::size_t>(indexCount));
      for (cgltf_size i = 0; i < indexCount; ++i) {
        indices.push_back(static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
      }

      const std::size_t added = splitHandednessConflicts(vertices, indices);
      if (added > 0) {
        const double growth = static_cast<double>(added) / static_cast<double>(vertexCount);
        summary.splitVertices += added;
        summary.meshesSplit += 1;
        if (growth > summary.maxSplitGrowth) {
          summary.maxSplitGrowth = growth;
          summary.maxSplitGrowthMesh = base;
        }
        reportLines.push_back(base + ": handedness split duplicated " + std::to_string(added) + " of " +
                              std::to_string(vertexCount) + " vertices (+" + formatPercent(growth) + ")");
      }

      atlantis::asset_system::TangentGenerationStats tangentStats;
      const auto tangents = atlantis::asset_system::generateTangentsU32(vertices, indices, &tangentStats);
      if (tangents.isErr()) return ResultT::Err(GltfImportError::TangentGenerationFailed);
      if (tangentStats.degenerateBasisFallbacks > 0) {
        summary.degenerateFallbackVertices += tangentStats.degenerateBasisFallbacks;
        summary.meshesWithDegenerateFallback += 1;
        reportLines.push_back(base + ": " + std::to_string(tangentStats.degenerateBasisFallbacks) +
                              " vertices took the degenerate-basis axis fallback (ADR-0073 Amendment 2026-09-19)");
      }

      // The u32-indices entry point encodes the real payload directly
      // (Plan 0037 M3) -- no u16 field in between, no >65535 special case.
      const std::vector<std::byte> artifactBytes =
          atlantis::asset_system::encodeMeshArtifactU32FromIndices(assetId, vertices, indices, tangents.value());

      atlantis::asset_system::AssetMetadata metadata;
      metadata.assetId = assetId;
      metadata.sourceLogicalPath = normalized;
      metadata.importerVersion = "atlantis_gltf_importer 1.0";
      metadata.assetType = "static_mesh";
      metadata.vertexCount = static_cast<std::uint32_t>(vertices.size());
      metadata.indexCount = static_cast<std::uint32_t>(indices.size());
      metadata.vertexStrideBytes = atlantis::asset_system::kMeshArtifactVertexStrideBytes;
      const std::string metadataText = atlantis::asset_system::serializeAssetMetadata(metadata);

      if (!writeBytes(staging.path / (base + ".amesh"), reinterpret_cast<const char*>(artifactBytes.data()),
                      artifactBytes.size()) ||
          !writeBytes(staging.path / (base + ".amesh.meta.txt"), metadataText.data(), metadataText.size())) {
        return ResultT::Err(GltfImportError::OutputWriteFailed);
      }

      summary.meshCount += 1;
      summary.totalVertices += static_cast<std::uint64_t>(vertices.size());
      summary.totalIndices += static_cast<std::uint64_t>(indexCount);
      if (vertices.size() > 65535) summary.meshesOverU16Range += 1;
      if (accessors.color != nullptr && accessors.color->type == cgltf_type_vec4) {
        reportLines.push_back(base + ": COLOR_0 alpha dropped (D7)");
      }
    }
  }

  reportLines.push_back("meshes: " + std::to_string(summary.meshCount) + ", vertices: " +
                        std::to_string(summary.totalVertices) + ", indices: " + std::to_string(summary.totalIndices) +
                        ", over_u16_range: " + std::to_string(summary.meshesOverU16Range));
  reportLines.push_back("handedness_split: " + std::to_string(summary.splitVertices) + " vertices duplicated in " +
                        std::to_string(summary.meshesSplit) + " meshes; max growth " +
                        formatPercent(summary.maxSplitGrowth) +
                        (summary.maxSplitGrowthMesh.empty() ? "" : " (" + summary.maxSplitGrowthMesh + ")"));
  reportLines.push_back("degenerate_basis_fallback: " + std::to_string(summary.degenerateFallbackVertices) +
                        " vertices in " + std::to_string(summary.meshesWithDegenerateFallback) + " meshes");
  std::vector<std::string> manifestLines;
  const auto materials =
      detail::writeMaterials(*guard.data, contentRoot, staging.path, name, summary, reportLines, manifestLines);
  if (materials.isErr()) return ResultT::Err(materials.error());
  std::string manifest =
      "# atlantis_gltf_importer cook manifest (Plan 0037). One atlantis_asset_cooker invocation per\n"
      "# non-comment line, in dependency order (textures, then materials). Substitute before running:\n"
      "#   {content_parent} = parent directory of --content-root\n"
      "#   {import_dir}     = this import's output directory\n"
      "#   {cooked_dir}     = the cooked-artifact output directory\n";
  for (const std::string& line : manifestLines) manifest += line + "\n";
  if (!writeBytes(staging.path / "cook_manifest.txt", manifest.data(), manifest.size())) {
    return ResultT::Err(GltfImportError::OutputWriteFailed);
  }

  std::string report;
  for (const std::string& line : reportLines) report += line + "\n";
  if (!writeBytes(staging.path / "import_report.txt", report.data(), report.size())) {
    return ResultT::Err(GltfImportError::OutputWriteFailed);
  }

  // Commit: the (absent or empty) output directory is replaced by the
  // fully-written staging directory in one rename.
  if (fs::exists(outputDir, ec)) fs::remove(outputDir, ec);
  if (!outputDir.parent_path().empty()) fs::create_directories(outputDir.parent_path(), ec);
  fs::rename(staging.path, outputDir, ec);
  if (ec) return ResultT::Err(GltfImportError::OutputWriteFailed);
  staging.committed = true;

  summary.reportLines = std::move(reportLines);
  return ResultT::Ok(std::move(summary));
}

}  // namespace atlantis::gltf_importer

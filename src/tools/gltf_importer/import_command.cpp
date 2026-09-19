#define CGLTF_IMPLEMENTATION
#include "import_command.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <cgltf.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>

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

struct CgltfDataGuard {
  cgltf_data* data = nullptr;
  ~CgltfDataGuard() {
    if (data != nullptr) cgltf_free(data);
  }
};

// ADR-0063's own numeric-contract tolerance, reused verbatim via the
// canonical Asset System helpers (never a restated constant -- the
// artifact decoder enforces the same pair independently).
[[nodiscard]] bool isNormalInTolerance(float x, float y, float z) {
  return atlantis::asset_system::detail::isNormalLengthSquaredInTolerance(
      atlantis::asset_system::detail::computeNormalLengthSquared(x, y, z));
}

[[nodiscard]] bool writeBytesAtomically(const fs::path& finalPath, const char* data, std::size_t size) {
  std::error_code ec;
  const fs::path dir = finalPath.parent_path();
  if (!dir.empty()) fs::create_directories(dir, ec);
  const fs::path tempPath = dir / (finalPath.filename().string() + ".tmp");
  {
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(data, static_cast<std::streamsize>(size));
    out.flush();
    if (!out.good()) {
      out.close();
      fs::remove(tempPath, ec);
      return false;
    }
  }
  fs::rename(tempPath, finalPath, ec);
  return !ec;
}

[[nodiscard]] bool writeTextAtomically(const fs::path& finalPath, const std::string& text) {
  return writeBytesAtomically(finalPath, text.data(), text.size());
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
        result.color = attribute.data;
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

}  // namespace

const char* gltfImportErrorMessage(GltfImportError error) noexcept {
  switch (error) {
    case GltfImportError::InputUnreadable:
      return "input .gltf unreadable";
    case GltfImportError::MalformedGltf:
      return "malformed or invalid glTF (cgltf parse/validate failed)";
    case GltfImportError::BufferUnreadable:
      return "buffer URI unreadable";
    case GltfImportError::NonTrianglesMode:
      return "primitive mode is not TRIANGLES";
    case GltfImportError::NonIndexedPrimitive:
      return "primitive is not indexed";
    case GltfImportError::MissingPosition:
      return "primitive has no POSITION attribute";
    case GltfImportError::UnsupportedIndexComponentType:
      return "index component type is not u8/u16/u32";
    case GltfImportError::OutOfRangeIndex:
      return "index value >= vertex count";
    case GltfImportError::OutOfRangeAccessor:
      return "attribute accessor count mismatch";
    case GltfImportError::NonFiniteVertex:
      return "non-finite vertex float";
    case GltfImportError::NonUnitNormal:
      return "normal outside unit-length tolerance";
    case GltfImportError::TangentGenerationFailed:
      return "tangent generation failed (degenerate basis or handedness conflict)";
    case GltfImportError::OutputWriteFailed:
      return "output write failed";
  }
  return "unknown gltf import error";
}

atlantis::Result<GltfImportSummary, GltfImportError> importGltfMeshes(const fs::path& inputPath,
                                                                       const fs::path& contentRoot,
                                                                       const fs::path& outputDir,
                                                                       const std::string& name) {
  using ResultT = atlantis::Result<GltfImportSummary, GltfImportError>;

  CgltfDataGuard guard;
  cgltf_options options{};
  const cgltf_result parseResult = cgltf_parse_file(&options, inputPath.string().c_str(), &guard.data);
  if (parseResult != cgltf_result_success || guard.data == nullptr) {
    return ResultT::Err(GltfImportError::MalformedGltf);
  }
  // cgltf concatenates relative buffer URIs onto this path verbatim --
  // it must end with a directory separator or the first path segment
  // merges ("content/bistro" + "bistro.bin").
  std::string contentDir = contentRoot.string();
  if (!contentDir.empty() && contentDir.back() != '/') contentDir += '/';
  const cgltf_result bufferResult = cgltf_load_buffers(&options, guard.data, contentDir.c_str());
  if (bufferResult != cgltf_result_success) return ResultT::Err(GltfImportError::BufferUnreadable);
  const cgltf_result validateResult = cgltf_validate(guard.data);
  if (validateResult != cgltf_result_success) return ResultT::Err(GltfImportError::MalformedGltf);

  GltfImportSummary summary;
  std::vector<std::string> reportLines;
  reportLines.push_back("gltf_mesh_import: " + name);

  for (cgltf_size meshIndex = 0; meshIndex < guard.data->meshes_count; ++meshIndex) {
    const cgltf_mesh& mesh = guard.data->meshes[meshIndex];
    for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
      const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
      if (primitive.type != cgltf_primitive_type_triangles) {
        return ResultT::Err(GltfImportError::NonTrianglesMode);
      }
      if (primitive.indices == nullptr) return ResultT::Err(GltfImportError::NonIndexedPrimitive);

      const PrimitiveAccessors accessors = attributesOf(primitive);
      if (accessors.position == nullptr) return ResultT::Err(GltfImportError::MissingPosition);

      const cgltf_size vertexCount = accessors.position->count;
      if (accessors.normal != nullptr && accessors.normal->count != vertexCount) {
        return ResultT::Err(GltfImportError::OutOfRangeAccessor);
      }
      if (accessors.texcoord != nullptr && accessors.texcoord->count != vertexCount) {
        return ResultT::Err(GltfImportError::OutOfRangeAccessor);
      }
      if (accessors.color != nullptr && accessors.color->count != vertexCount) {
        return ResultT::Err(GltfImportError::OutOfRangeAccessor);
      }

      // Mesh names are not unique across the file (Bistro's three
      // >65535-vertex meshes are all named "subset_1"), so the logical
      // path is index-based -- always a valid logical path by construction.
      const std::string logicalPath = "meshes/" + name + "/mesh_" + std::to_string(meshIndex) + "_" +
                                      std::to_string(primitiveIndex);
      const auto normalizedResult = atlantis::asset_system::normalizeLogicalPath(logicalPath);
      if (normalizedResult.isErr()) return ResultT::Err(GltfImportError::OutputWriteFailed);
      const std::string& normalized = normalizedResult.value();
      const atlantis::asset_system::AssetId assetId = atlantis::asset_system::computeAssetId(normalized);

      atlantis::asset_system::ParsedMeshSource source;
      source.vertices.reserve(static_cast<std::size_t>(vertexCount));
      bool hasTangent = accessors.tangent != nullptr;
      bool hasColor = accessors.color != nullptr;
      if (hasTangent) {
        reportLines.push_back("mesh_" + std::to_string(meshIndex) + "_" + std::to_string(primitiveIndex) +
                              ": upstream TANGENT discarded (D8)");
      }

      for (cgltf_size v = 0; v < vertexCount; ++v) {
        atlantis::asset_system::MeshSourceVertex vertex;
        float position[3];
        if (cgltf_accessor_read_float(accessors.position, v, position, 3) != 1) {
          return ResultT::Err(GltfImportError::OutOfRangeAccessor);
        }
        vertex.positionX = position[0];
        vertex.positionY = position[1];
        vertex.positionZ = position[2];

        if (accessors.normal != nullptr) {
          float normal[3];
          if (cgltf_accessor_read_float(accessors.normal, v, normal, 3) != 1) {
            return ResultT::Err(GltfImportError::OutOfRangeAccessor);
          }
          vertex.normalX = normal[0];
          vertex.normalY = normal[1];
          vertex.normalZ = normal[2];
        } else {
          vertex.normalX = 0.0f;
          vertex.normalY = 0.0f;
          vertex.normalZ = 1.0f;
        }

        if (accessors.texcoord != nullptr) {
          float uv[2];
          if (cgltf_accessor_read_float(accessors.texcoord, v, uv, 2) != 1) {
            return ResultT::Err(GltfImportError::OutOfRangeAccessor);
          }
          vertex.uvU = uv[0];
          vertex.uvV = uv[1];
        }

        // D7: absent COLOR_0 -> white; present -> RGB read, alpha dropped.
        if (accessors.color != nullptr) {
          float color[4];
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

        if (!std::isfinite(vertex.positionX) || !std::isfinite(vertex.positionY) ||
            !std::isfinite(vertex.positionZ) || !std::isfinite(vertex.uvU) || !std::isfinite(vertex.uvV)) {
          return ResultT::Err(GltfImportError::NonFiniteVertex);
        }
        if (!isNormalInTolerance(vertex.normalX, vertex.normalY, vertex.normalZ)) {
          std::fprintf(stderr, "normal fail: mesh=%d prim=%d v=%d (%f,%f,%f)\n",
                       static_cast<int>(meshIndex), static_cast<int>(primitiveIndex), static_cast<int>(v),
                       vertex.normalX, vertex.normalY, vertex.normalZ);
          return ResultT::Err(GltfImportError::NonUnitNormal);
        }

        source.vertices.push_back(vertex);
      }

      const cgltf_size indexCount = primitive.indices->count;
      std::vector<std::uint32_t> indices;
      indices.reserve(static_cast<std::size_t>(indexCount));
      for (cgltf_size i = 0; i < indexCount; ++i) {
        const cgltf_uint indexValue =
            static_cast<cgltf_uint>(cgltf_accessor_read_index(primitive.indices, i));
        if (indexValue >= vertexCount) return ResultT::Err(GltfImportError::OutOfRangeIndex);
        indices.push_back(static_cast<std::uint32_t>(indexValue));
      }

      const auto tangents = atlantis::asset_system::generateTangentsU32(source.vertices, indices);
      if (tangents.isErr()) return ResultT::Err(GltfImportError::TangentGenerationFailed);

      // The u32-indices entry point encodes the real payload directly
      // (Plan 0037 M3) -- no u16 field in between, no >65535 special case.
      const std::vector<std::byte> artifactBytes =
          atlantis::asset_system::encodeMeshArtifactU32FromIndices(assetId, source.vertices, indices,
                                                                   tangents.value());

      atlantis::asset_system::AssetMetadata metadata;
      metadata.assetId = assetId;
      metadata.sourceLogicalPath = normalized;
      metadata.importerVersion = "atlantis_gltf_importer 1.0";
      metadata.assetType = "static_mesh";
      metadata.vertexCount = static_cast<std::uint32_t>(vertexCount);
      metadata.indexCount = static_cast<std::uint32_t>(indexCount);
      metadata.vertexStrideBytes = atlantis::asset_system::kMeshArtifactVertexStrideBytes;
      const std::string metadataText = atlantis::asset_system::serializeAssetMetadata(metadata);

      const std::string base = name + "_mesh_" + std::to_string(meshIndex) + "_" + std::to_string(primitiveIndex);
      if (!writeBytesAtomically(outputDir / (base + ".amesh"),
                                reinterpret_cast<const char*>(artifactBytes.data()), artifactBytes.size())) {
        return ResultT::Err(GltfImportError::OutputWriteFailed);
      }
      if (!writeTextAtomically(outputDir / (base + ".amesh.meta.txt"), metadataText)) {
        return ResultT::Err(GltfImportError::OutputWriteFailed);
      }

      summary.meshCount += 1;
      summary.totalVertices += static_cast<std::uint64_t>(vertexCount);
      summary.totalIndices += static_cast<std::uint64_t>(indexCount);
      if (vertexCount > 65535) summary.meshesOverU16Range += 1;
      if (hasColor) {
        reportLines.push_back(base + ": COLOR_0 alpha dropped (D7)");
      }
    }
  }

  reportLines.push_back("meshes: " + std::to_string(summary.meshCount) + ", vertices: " +
                        std::to_string(summary.totalVertices) + ", indices: " + std::to_string(summary.totalIndices) +
                        ", over_u16_range: " + std::to_string(summary.meshesOverU16Range));
  std::string report;
  for (const std::string& line : reportLines) report += line + "\n";
  if (!writeTextAtomically(outputDir / "import_report.txt", report)) {
    return ResultT::Err(GltfImportError::OutputWriteFailed);
  }

  summary.reportLines = std::move(reportLines);
  return ResultT::Ok(std::move(summary));
}

}  // namespace atlantis::gltf_importer

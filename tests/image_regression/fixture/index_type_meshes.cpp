#include "index_type_meshes.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/asset_system/mesh_tangent_generation.h>

#include <cstddef>
#include <fstream>
#include <vector>

namespace atlantis::image_regression {

namespace {

namespace as = atlantis::asset_system;

void writeArtifact(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeSidecar(const std::filesystem::path& path, as::AssetId assetId, const std::string& logicalPath,
                  std::uint32_t vertexCount, std::uint32_t indexCount) {
  as::AssetMetadata metadata;
  metadata.assetId = assetId;
  metadata.sourceLogicalPath = logicalPath;
  metadata.importerVersion = "spec_0039_fixture";
  metadata.assetType = "static_mesh";
  metadata.vertexCount = vertexCount;
  metadata.indexCount = indexCount;
  metadata.vertexStrideBytes = as::kMeshArtifactVertexStrideBytes;

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << as::serializeAssetMetadata(metadata);
}

// The exact eight vertices and thirty-six indices of
// assets/meshes/minimal_cube.mesh.txt.
[[nodiscard]] as::ParsedMeshSource equivalenceCubeSource() {
  constexpr float kN = 0.577350269f;
  as::ParsedMeshSource source;
  source.vertices = {
      {-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -kN, -kN, -kN},
      {0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, kN, -kN, -kN},
      {0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, kN, kN, -kN},
      {-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -kN, kN, -kN},
      {-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -kN, -kN, kN},
      {0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, kN, -kN, kN},
      {0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, kN, kN, kN},
      {-0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -kN, kN, kN},
  };
  source.indices = {0, 1, 2, 2, 3, 0, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4,
                    1, 5, 6, 6, 2, 1, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
  return source;
}

}  // namespace

MeshArtifactPaths writeEquivalenceCubeV4(const std::filesystem::path& directory) {
  const as::ParsedMeshSource source = equivalenceCubeSource();
  const auto tangents = as::generateTangents(source);
  const std::string logicalPath = "spec_0039/equivalence_cube_v4.mesh.txt";
  const as::AssetId assetId = as::computeAssetId(logicalPath);

  const std::filesystem::path artifactPath = directory / "equivalence_cube_v4.amesh";
  const std::filesystem::path metadataPath = directory / "equivalence_cube_v4.amesh.meta.txt";
  writeArtifact(artifactPath, as::encodeMeshArtifact(assetId, source, tangents.value()));
  writeSidecar(metadataPath, assetId, logicalPath, static_cast<std::uint32_t>(source.vertices.size()),
               static_cast<std::uint32_t>(source.indices.size()));
  return {artifactPath.string(), metadataPath.string()};
}

MeshArtifactPaths writeEquivalenceCubeV5(const std::filesystem::path& directory) {
  const as::ParsedMeshSource source = equivalenceCubeSource();
  // Same source object, same generateTangents() call, same encoder
  // family -- encodeMeshArtifactU32() takes the identical inputs its v4
  // sibling does, which is what makes the vertex bytes identical rather
  // than merely similar.
  const auto tangents = as::generateTangents(source);
  const std::string logicalPath = "spec_0039/equivalence_cube_v5.mesh.txt";
  const as::AssetId assetId = as::computeAssetId(logicalPath);

  const std::filesystem::path artifactPath = directory / "equivalence_cube_v5.amesh";
  const std::filesystem::path metadataPath = directory / "equivalence_cube_v5.amesh.meta.txt";
  writeArtifact(artifactPath, as::encodeMeshArtifactU32(assetId, source, tangents.value()));
  writeSidecar(metadataPath, assetId, logicalPath, static_cast<std::uint32_t>(source.vertices.size()),
               static_cast<std::uint32_t>(source.indices.size()));
  return {artifactPath.string(), metadataPath.string()};
}

MeshArtifactPaths writeLargeIndexGridV5(const std::filesystem::path& directory) {
  constexpr std::uint32_t kSide = kLargeIndexGridVerticesPerSide;
  constexpr float kExtent = 0.5f;

  std::vector<as::MeshSourceVertex> vertices;
  std::vector<as::VertexTangent> tangents;
  vertices.reserve(kLargeIndexGridVertexCount);
  tangents.reserve(kLargeIndexGridVertexCount);

  for (std::uint32_t row = 0; row < kSide; ++row) {
    for (std::uint32_t column = 0; column < kSide; ++column) {
      const std::uint32_t index = row * kSide + column;
      const float u = static_cast<float>(column) / static_cast<float>(kSide - 1);
      const float v = static_cast<float>(row) / static_cast<float>(kSide - 1);

      // Row 0 is the top edge, so the high-index band -- the grid's
      // second half -- is the lower half of the rendered quad.
      const bool highBand = index >= kLargeIndexGridHighBandFirstVertex;

      as::MeshSourceVertex vertex;
      vertex.positionX = -kExtent + u * (2.0f * kExtent);
      vertex.positionY = kExtent - v * (2.0f * kExtent);
      vertex.positionZ = 0.0f;
      vertex.colorR = highBand ? 0.0f : 1.0f;
      vertex.colorG = 0.0f;
      vertex.colorB = highBand ? 1.0f : 0.0f;
      vertex.uvU = u;
      vertex.uvV = v;
      vertex.normalX = 0.0f;
      vertex.normalY = 0.0f;
      vertex.normalZ = 1.0f;
      vertices.push_back(vertex);

      tangents.push_back(as::VertexTangent{1.0f, 0.0f, 0.0f, 1.0f});
    }
  }

  std::vector<std::uint32_t> indices;
  indices.reserve(kLargeIndexGridIndexCount);
  for (std::uint32_t row = 0; row + 1 < kSide; ++row) {
    for (std::uint32_t column = 0; column + 1 < kSide; ++column) {
      const std::uint32_t topLeft = row * kSide + column;
      const std::uint32_t topRight = topLeft + 1;
      const std::uint32_t bottomLeft = topLeft + kSide;
      const std::uint32_t bottomRight = bottomLeft + 1;
      indices.insert(indices.end(), {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight});
    }
  }

  const std::string logicalPath = "spec_0039/large_index_grid.mesh.txt";
  const as::AssetId assetId = as::computeAssetId(logicalPath);

  const std::filesystem::path artifactPath = directory / "large_index_grid.amesh";
  const std::filesystem::path metadataPath = directory / "large_index_grid.amesh.meta.txt";
  writeArtifact(artifactPath, as::encodeMeshArtifactU32FromIndices(assetId, vertices, indices, tangents));
  writeSidecar(metadataPath, assetId, logicalPath, static_cast<std::uint32_t>(vertices.size()),
               static_cast<std::uint32_t>(indices.size()));
  return {artifactPath.string(), metadataPath.string()};
}

}  // namespace atlantis::image_regression

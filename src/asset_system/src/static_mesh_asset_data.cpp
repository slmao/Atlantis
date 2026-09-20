#include <atlantis/asset_system/static_mesh_asset_data.h>

#include <atlantis/assert.h>

namespace atlantis::asset_system {

StaticMeshAssetData::StaticMeshAssetData(std::vector<std::byte> vertexBytes, std::vector<std::uint16_t> indices,
                                          std::uint32_t vertexStrideBytes) noexcept
    : vertexBytes_(std::move(vertexBytes)),
      indices_(std::move(indices)),
      vertexStrideBytes_(vertexStrideBytes),
      indexType_(MeshIndexType::Uint16) {}

StaticMeshAssetData::StaticMeshAssetData(std::vector<std::byte> vertexBytes, std::vector<std::uint32_t> indices,
                                          std::uint32_t vertexStrideBytes) noexcept
    : vertexBytes_(std::move(vertexBytes)),
      indices32_(std::move(indices)),
      vertexStrideBytes_(vertexStrideBytes),
      indexType_(MeshIndexType::Uint32) {}

const std::vector<std::uint16_t>& StaticMeshAssetData::indices() const noexcept {
  ATLANTIS_CHECK_MSG(indexType_ == MeshIndexType::Uint16,
                     "StaticMeshAssetData::indices(): this asset carries 32-bit indices (.amesh schema 5) -- "
                     "check indexType() and read indices32()");
  return indices_;
}

const std::vector<std::uint32_t>& StaticMeshAssetData::indices32() const noexcept {
  ATLANTIS_CHECK_MSG(indexType_ == MeshIndexType::Uint32,
                     "StaticMeshAssetData::indices32(): this asset carries 16-bit indices (.amesh schema 4) -- "
                     "check indexType() and read indices()");
  return indices32_;
}

std::uint32_t StaticMeshAssetData::vertexCount() const noexcept {
  ATLANTIS_CHECK(vertexStrideBytes_ != 0);
  return static_cast<std::uint32_t>(vertexBytes_.size() / vertexStrideBytes_);
}

std::uint32_t StaticMeshAssetData::indexCount() const noexcept {
  // Exactly one of the two is ever non-empty, so the sum is the count
  // without a branch on indexType_ -- and stays correct if a future
  // schema ever adds a third width.
  return static_cast<std::uint32_t>(indices_.size() + indices32_.size());
}

}  // namespace atlantis::asset_system

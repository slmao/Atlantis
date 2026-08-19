#include <atlantis/asset_system/static_mesh_asset_data.h>

#include <atlantis/assert.h>

namespace atlantis::asset_system {

StaticMeshAssetData::StaticMeshAssetData(std::vector<std::byte> vertexBytes, std::vector<std::uint16_t> indices,
                                          std::uint32_t vertexStrideBytes) noexcept
    : vertexBytes_(std::move(vertexBytes)), indices_(std::move(indices)), vertexStrideBytes_(vertexStrideBytes) {}

std::uint32_t StaticMeshAssetData::vertexCount() const noexcept {
  ATLANTIS_CHECK(vertexStrideBytes_ != 0);
  return static_cast<std::uint32_t>(vertexBytes_.size() / vertexStrideBytes_);
}

std::uint32_t StaticMeshAssetData::indexCount() const noexcept {
  return static_cast<std::uint32_t>(indices_.size());
}

}  // namespace atlantis::asset_system

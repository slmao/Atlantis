#include <atlantis/asset_system/static_mesh_asset_data.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>

using atlantis::asset_system::StaticMeshAssetData;

TEST_CASE("StaticMeshAssetData exposes the bytes/indices/stride it was constructed with", "[asset_system]") {
  std::vector<std::byte> vertexBytes(24 * 3, std::byte{0xAB});
  std::vector<std::uint16_t> indices{0, 1, 2};
  StaticMeshAssetData data(vertexBytes, indices, 24);

  CHECK(data.vertexBytes() == vertexBytes);
  CHECK(data.indices() == indices);
  CHECK(data.vertexStrideBytes() == 24);
  CHECK(data.vertexCount() == 3);
  CHECK(data.indexCount() == 3);
}

TEST_CASE("StaticMeshAssetData is move-constructible and move-assignable", "[asset_system]") {
  StaticMeshAssetData original(std::vector<std::byte>(24, std::byte{0x01}), std::vector<std::uint16_t>{0}, 24);

  StaticMeshAssetData moved(std::move(original));
  CHECK(moved.vertexCount() == 1);
  CHECK(moved.indexCount() == 1);

  StaticMeshAssetData other(std::vector<std::byte>{}, std::vector<std::uint16_t>{}, 24);
  other = std::move(moved);
  CHECK(other.vertexCount() == 1);
  CHECK(other.indexCount() == 1);
}

TEST_CASE("StaticMeshAssetData vertexCount divides vertexBytes size by stride", "[asset_system]") {
  StaticMeshAssetData data(std::vector<std::byte>(24 * 8, std::byte{0}), std::vector<std::uint16_t>(36, 0), 24);
  CHECK(data.vertexCount() == 8);
  CHECK(data.indexCount() == 36);
}

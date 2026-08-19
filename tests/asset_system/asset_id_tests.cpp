#include <atlantis/asset_system/asset_id.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

TEST_CASE("computeAssetId matches known FNV-1a 64-bit test vectors", "[asset_system]") {
  // Independently computed (not transcribed from memory) against the
  // standard FNV-1a 64-bit algorithm: offset basis 0xcbf29ce484222325,
  // prime 0x100000001b3.
  CHECK(computeAssetId("") == 0xcbf29ce484222325ULL);
  CHECK(computeAssetId("a") == 0xaf63dc4c8601ec8cULL);
  CHECK(computeAssetId("foobar") == 0x85944171f73967e8ULL);
  CHECK(computeAssetId("meshes/minimal_cube.mesh.txt") == 0x78c473ee2218581dULL);
}

TEST_CASE("computeAssetId is deterministic for the same input", "[asset_system]") {
  const AssetId first = computeAssetId("meshes/minimal_cube.mesh.txt");
  const AssetId second = computeAssetId("meshes/minimal_cube.mesh.txt");
  CHECK(first == second);
}

TEST_CASE("computeAssetId differs for different logical paths", "[asset_system]") {
  CHECK(computeAssetId("meshes/a.mesh.txt") != computeAssetId("meshes/b.mesh.txt"));
}

TEST_CASE("toHexString produces a fixed-width, lowercase, 16-hex-digit form", "[asset_system]") {
  CHECK(toHexString(0) == "0000000000000000");
  CHECK(toHexString(0xaf63dc4c8601ec8cULL) == "af63dc4c8601ec8c");
  CHECK(toHexString(0xFFFFFFFFFFFFFFFFULL) == "ffffffffffffffff");
  CHECK(toHexString(computeAssetId("a")).size() == 16);
}

TEST_CASE("Little-endian serialization round-trips and matches explicit byte order", "[asset_system]") {
  constexpr AssetId id = 0x0102030405060708ULL;
  const auto bytes = toLittleEndianBytes(id);

  // Byte 0 is the least-significant byte, regardless of host endianness.
  CHECK(bytes[0] == std::byte{0x08});
  CHECK(bytes[1] == std::byte{0x07});
  CHECK(bytes[2] == std::byte{0x06});
  CHECK(bytes[3] == std::byte{0x05});
  CHECK(bytes[4] == std::byte{0x04});
  CHECK(bytes[5] == std::byte{0x03});
  CHECK(bytes[6] == std::byte{0x02});
  CHECK(bytes[7] == std::byte{0x01});

  CHECK(fromLittleEndianBytes(bytes) == id);
}

TEST_CASE("Little-endian serialization round-trips for computed Asset IDs", "[asset_system]") {
  const AssetId id = computeAssetId("meshes/minimal_cube.mesh.txt");
  CHECK(fromLittleEndianBytes(toLittleEndianBytes(id)) == id);
}

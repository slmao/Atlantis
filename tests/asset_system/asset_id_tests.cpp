#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>

#include "fnv1a64.h"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string_view>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] AssetId fnv1a64Of(std::string_view text) {
  return detail::fnv1a64(std::as_bytes(std::span(text.data(), text.size())));
}

}  // namespace

TEST_CASE("The internal FNV-1a-64 core matches known test vectors", "[asset_system]") {
  // Independently computed (not transcribed from memory) against the
  // standard FNV-1a 64-bit algorithm: offset basis 0xcbf29ce484222325,
  // prime 0x100000001b3.
  CHECK(fnv1a64Of("") == 0xcbf29ce484222325ULL);
  CHECK(fnv1a64Of("a") == 0xaf63dc4c8601ec8cULL);
  CHECK(fnv1a64Of("foobar") == 0x85944171f73967e8ULL);
  CHECK(fnv1a64Of("meshes/minimal_cube.mesh.txt") == 0x78c473ee2218581dULL);
}

TEST_CASE("assetKey is the FNV-1a-64 core over the GUID's 16 bytes", "[asset_system]") {
  const AssetGuid guid = parseAssetGuid("01234567-89ab-4def-8123-456789abcdef").value();
  CHECK(assetKey(guid) == detail::fnv1a64(guid.bytes));
}

TEST_CASE("toHexString produces a fixed-width, lowercase, 16-hex-digit form", "[asset_system]") {
  CHECK(toHexString(0) == "0000000000000000");
  CHECK(toHexString(0xaf63dc4c8601ec8cULL) == "af63dc4c8601ec8c");
  CHECK(toHexString(0xFFFFFFFFFFFFFFFFULL) == "ffffffffffffffff");
  CHECK(toHexString(0x1ULL).size() == 16);
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

TEST_CASE("Little-endian serialization round-trips for a GUID's key", "[asset_system]") {
  const AssetId id = assetKey(parseAssetGuid("01234567-89ab-4def-8123-456789abcdef").value());
  CHECK(fromLittleEndianBytes(toLittleEndianBytes(id)) == id);
}

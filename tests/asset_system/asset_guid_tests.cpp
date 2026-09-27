// Plan 0047 M1 (P2, P3; ADR-0097 D1, D2, D4): GUID codecs, FNV-1a-128,
// derivation and the 64-bit key. Every derivation and key vector below is
// pinned: a change to any of them changes persisted identities.

#include <catch2/catch_test_macros.hpp>

#include <atlantis/asset_system/asset_guid.h>

#include <set>
#include <span>
#include <string>
#include <string_view>

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::assetGuidFromBytes;
using atlantis::asset_system::assetKey;
using atlantis::asset_system::deriveAssetGuid;
using atlantis::asset_system::deriveEntityGuid;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::entityGuidFromBytes;
using atlantis::asset_system::fnv1a128;
using atlantis::asset_system::GuidBytes;
using atlantis::asset_system::GuidParseError;
using atlantis::asset_system::parseAssetGuid;
using atlantis::asset_system::parseEntityGuid;
using atlantis::asset_system::toString;

namespace {

constexpr std::string_view kOwnerText = "01234567-89ab-4def-8123-456789abcdef";

[[nodiscard]] AssetGuid owner() { return parseAssetGuid(kOwnerText).value(); }

[[nodiscard]] std::string fnvHex(std::string_view input) {
  const GuidBytes bytes = fnv1a128(std::as_bytes(std::span<const char>(input.data(), input.size())));
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex;
  for (std::byte b : bytes) {
    hex.push_back(kHexDigits[static_cast<unsigned>(b) >> 4]);
    hex.push_back(kHexDigits[static_cast<unsigned>(b) & 0xFU]);
  }
  return hex;
}

}  // namespace

TEST_CASE("AssetGuid and EntityGuid text round-trip in canonical form", "[asset_guid]") {
  const auto asset = parseAssetGuid(kOwnerText);
  REQUIRE(asset.isOk());
  CHECK(toString(asset.value()) == kOwnerText);

  const auto entity = parseEntityGuid(kOwnerText);
  REQUIRE(entity.isOk());
  CHECK(toString(entity.value()) == kOwnerText);
  CHECK(entity.value().bytes == asset.value().bytes);
}

TEST_CASE("GUID bytes are stored in text order", "[asset_guid]") {
  const AssetGuid guid = owner();
  CHECK(guid.bytes[0] == std::byte{0x01});
  CHECK(guid.bytes[3] == std::byte{0x67});
  CHECK(guid.bytes[6] == std::byte{0x4d});
  CHECK(guid.bytes[8] == std::byte{0x81});
  CHECK(guid.bytes[15] == std::byte{0xef});
}

TEST_CASE("GUID ordering follows canonical text ordering", "[asset_guid]") {
  const AssetGuid lower = parseAssetGuid("01234567-89ab-4def-8123-456789abcdee").value();
  const AssetGuid higher = parseAssetGuid("11234567-89ab-4def-8123-456789abcdee").value();
  CHECK(lower < owner());
  CHECK(owner() < higher);
  CHECK(owner() == parseAssetGuid(kOwnerText).value());
}

TEST_CASE("GUID text parsing rejects each malformation with its own error", "[asset_guid]") {
  CHECK(parseAssetGuid("").error() == GuidParseError::WrongLength);
  CHECK(parseAssetGuid("0123456789ab4def8123456789abcdef").error() == GuidParseError::WrongLength);
  CHECK(parseAssetGuid("01234567-89ab-4def-8123-456789abcdef0").error() == GuidParseError::WrongLength);
  CHECK(parseAssetGuid("01234567_89ab-4def-8123-456789abcdef").error() == GuidParseError::MissingHyphen);
  CHECK(parseAssetGuid("01234567-89ab-4def-8123x456789abcdef").error() == GuidParseError::MissingHyphen);
  CHECK(parseAssetGuid("01234567-89AB-4def-8123-456789abcdef").error() == GuidParseError::NotLowercaseHex);
  CHECK(parseAssetGuid("0123456g-89ab-4def-8123-456789abcdef").error() == GuidParseError::NotLowercaseHex);
  CHECK(parseAssetGuid("0123-567-89ab-4def-8123-456789abcdef").error() == GuidParseError::NotLowercaseHex);
  CHECK(parseAssetGuid("00000000-0000-0000-0000-000000000000").error() == GuidParseError::NilGuid);
  CHECK(parseEntityGuid("00000000-0000-0000-0000-000000000000").error() == GuidParseError::NilGuid);
  CHECK(parseEntityGuid("01234567-89ab-4def-8123-456789abcdeF").error() == GuidParseError::NotLowercaseHex);
}

TEST_CASE("GUID binary form rejects only the nil GUID", "[asset_guid]") {
  CHECK(assetGuidFromBytes(GuidBytes{}).error() == GuidParseError::NilGuid);
  CHECK(entityGuidFromBytes(GuidBytes{}).error() == GuidParseError::NilGuid);

  GuidBytes one{};
  one[15] = std::byte{1};
  REQUIRE(assetGuidFromBytes(one).isOk());
  CHECK(toString(assetGuidFromBytes(one).value()) == "00000000-0000-0000-0000-000000000001");
  CHECK(assetGuidFromBytes(owner().bytes).value() == owner());
  CHECK(entityGuidFromBytes(owner().bytes).value().bytes == owner().bytes);
}

TEST_CASE("FNV-1a-128 matches the published reference vectors", "[asset_guid]") {
  CHECK(fnvHex("") == "6c62272e07bb014262b821756295c58d");  // the offset basis
  CHECK(fnvHex("a") == "d228cb696f1a8caf78912b704e4a8964");
  CHECK(fnvHex("foobar") == "343e1662793c64bf6f0d3597ba446f18");
}

TEST_CASE("Derived GUIDs match their pinned vectors", "[asset_guid]") {
  CHECK(toString(deriveAssetGuid(owner(), "scene")) == "3cce1386-644f-85a7-ba8a-eff0ec933b8b");
  CHECK(toString(deriveAssetGuid(owner(), "mesh/0/0")) == "e5821d7c-3626-80ff-baf9-a7c64f3d34f4");
  CHECK(toString(deriveAssetGuid(owner(), "mesh/12/1")) == "2d61f16b-2d74-8a0d-9131-7101285a4506");
  CHECK(toString(deriveAssetGuid(owner(), "material/3")) == "9574554c-b53e-8452-b81f-dd4f968a2a42");
  CHECK(toString(deriveAssetGuid(owner(), "texture/textures/wall.dds")) == "a86035bf-6f7d-8b9b-ac56-b349ed9906ad");
  CHECK(toString(deriveAssetGuid(owner(), "fallback/white")) == "0c384826-698e-8fd1-bb20-0db2e611c451");
  CHECK(toString(deriveEntityGuid(owner(), "node/7")) == "30ac261e-184f-8513-8899-19dff47909c3");
}

TEST_CASE("Derived GUIDs carry version 8 and the RFC 9562 variant", "[asset_guid]") {
  for (std::string_view subKey : {"scene", "mesh/0/0", "material/3", "node/7", ""}) {
    const AssetGuid derived = deriveAssetGuid(owner(), subKey);
    CHECK((derived.bytes[6] & std::byte{0xF0}) == std::byte{0x80});
    CHECK((derived.bytes[8] & std::byte{0xC0}) == std::byte{0x80});
  }
}

TEST_CASE("Derivation is deterministic and separates sub-keys and owners", "[asset_guid]") {
  CHECK(deriveAssetGuid(owner(), "mesh/1/0") == deriveAssetGuid(owner(), "mesh/1/0"));
  CHECK(deriveEntityGuid(owner(), "node/1").bytes == deriveAssetGuid(owner(), "node/1").bytes);

  std::set<AssetGuid> seen;
  for (int mesh = 0; mesh < 64; ++mesh) {
    for (int primitive = 0; primitive < 4; ++primitive) {
      seen.insert(deriveAssetGuid(owner(), "mesh/" + std::to_string(mesh) + "/" + std::to_string(primitive)));
    }
  }
  CHECK(seen.size() == 64 * 4);

  const AssetGuid otherOwner = parseAssetGuid("fedcba98-7654-4321-8fed-cba987654321").value();
  CHECK(deriveAssetGuid(owner(), "scene") != deriveAssetGuid(otherOwner, "scene"));
}

TEST_CASE("assetKey matches its pinned vectors", "[asset_guid]") {
  CHECK(assetKey(owner()) == 0x030a3e5ba83d98a5ULL);
  CHECK(assetKey(parseAssetGuid("00000000-0000-0000-0000-000000000001").value()) == 0x88201eb960ff62b2ULL);
  CHECK(assetKey(parseAssetGuid("ffffffff-ffff-ffff-ffff-ffffffffffff").value()) == 0xd6607508f5a1e855ULL);
}

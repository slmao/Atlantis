// Plan 0047 M1 (P4; ADR-0097 D1): minting lives only in the cooker, behind
// --kind=mint-guid, and never runs from a cook manifest.

#include <catch2/catch_test_macros.hpp>

#include "cook_command.h"
#include "guid_mint.h"

#include <atlantis/asset_system/asset_guid.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using atlantis::asset_system::AssetGuid;
using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::mintAssetGuid;
using atlantis::tools::asset_cooker::mintAssetGuids;
using atlantis::tools::asset_cooker::parseCookArguments;
using atlantis::tools::asset_cooker::runCookCommand;

namespace {

[[nodiscard]] bool isVersion4(const AssetGuid& guid) {
  return (guid.bytes[6] & std::byte{0xF0}) == std::byte{0x40} && (guid.bytes[8] & std::byte{0xC0}) == std::byte{0x80};
}

}  // namespace

TEST_CASE("Minted GUIDs carry version 4 and the RFC 9562 variant", "[guid_mint]") {
  for (const AssetGuid& guid : mintAssetGuids(64)) CHECK(isVersion4(guid));
}

TEST_CASE("Minted GUIDs differ", "[guid_mint]") {
  const std::vector<AssetGuid> guids = mintAssetGuids(1000);
  const std::set<AssetGuid> distinct(guids.begin(), guids.end());
  CHECK(distinct.size() == guids.size());
}

TEST_CASE("An all-zero draw is retried", "[guid_mint]") {
  std::uint32_t calls = 0;
  const AssetGuid guid = mintAssetGuid([&calls]() -> std::uint32_t { return calls++ < 4 ? 0U : 0x01020304U; });
  CHECK(calls == 8);
  CHECK(guid.bytes[0] == std::byte{0x01});
  CHECK(guid.bytes[15] == std::byte{0x04});
  CHECK(isVersion4(guid));
}

TEST_CASE("The mint-guid kind parses with and without a count", "[guid_mint]") {
  std::ostringstream err;
  CookCommandRequest one;
  REQUIRE(parseCookArguments({"--kind=mint-guid"}, one, err));
  CHECK(one.kind == AssetKind::MintGuid);
  CHECK(one.mintCount == 1);

  CookCommandRequest three;
  REQUIRE(parseCookArguments({"--kind=mint-guid", "--count=3"}, three, err));
  CHECK(three.mintCount == 3);
}

TEST_CASE("The mint count must be a positive integer", "[guid_mint]") {
  for (const char* bad : {"--count=0", "--count=-1", "--count=3x", "--count="}) {
    std::ostringstream err;
    CookCommandRequest request;
    CHECK_FALSE(parseCookArguments({"--kind=mint-guid", bad}, request, err));
    CHECK_FALSE(err.str().empty());
  }
}

TEST_CASE("A cook manifest line cannot mint", "[guid_mint]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atlantis_guid_mint_manifest_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "cook_manifest.txt") << "--kind=mint-guid --count=2\n";

  CookCommandRequest request;
  request.kind = AssetKind::CookManifest;
  request.importDir = dir.string();
  request.cookedDir = (dir / "cooked").string();
  request.contentParent = dir.string();
  CHECK(runCookCommand(request) != 0);
  CHECK_FALSE(std::filesystem::exists(dir / "import.catalog.txt"));

  std::filesystem::remove_all(dir);
}

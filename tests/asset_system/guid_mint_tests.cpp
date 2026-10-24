#include <atlantis/asset_system/guid_mint.h>

#include <catch2/catch_test_macros.hpp>

#include <set>

// Plan 0065 J8 (ADR-0130 D1): the mint, moved from the asset cooker into
// Asset System, still the only minting code. mintAssetGuid()'s draw, retry and
// bit layout stay covered by tests/tools/asset_cooker/guid_mint_tests.cpp; this
// covers mintEntityGuid(), the authoring session's CreateNode mint.

using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::mintEntityGuid;

TEST_CASE("mintEntityGuid: version 4, the RFC 9562 variant, non-nil and distinct", "[asset_system][guid_mint]") {
  std::set<EntityGuid> seen;
  for (int i = 0; i < 64; ++i) {
    const EntityGuid guid = mintEntityGuid();
    CHECK(guid != EntityGuid{});
    CHECK((guid.bytes[6] & std::byte{0xF0}) == std::byte{0x40});
    CHECK((guid.bytes[8] & std::byte{0xC0}) == std::byte{0x80});
    seen.insert(guid);
  }
  CHECK(seen.size() == 64);
}

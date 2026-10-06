#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <atlantis/asset_system/asset_system_schema.h>
#include <atlantis/schema.h>
#include <atlantis/world/world_schema.h>

// Plan 0048 M4 (Spec 0048 R3, Testing plan "Cross-module uniqueness"): the
// combined World + Asset System tables hold no duplicate TypeId, FieldId or
// qualified type name. Lives in tests/world because World links both modules.

TEST_CASE("schema: TypeIds, FieldIds and type names are unique across modules", "[world][schema]") {
  std::vector<atlantis::schema::TypeDescriptor> combined;
  for (const auto& type : atlantis::world::worldSchema()) combined.push_back(type);
  for (const auto& type : atlantis::asset_system::assetSystemSchema()) combined.push_back(type);
  REQUIRE(combined.size() == 21);  // Plan 0049 M1: 13 -> 20, the seven scene types; Plan 0051 M1: 21, WorldMatrix

  std::set<std::uint64_t> typeIds;
  std::set<std::uint64_t> fieldIds;
  std::set<std::string_view> names;
  std::size_t fieldCount = 0;
  for (const auto& type : combined) {
    INFO(type.name);
    CHECK(typeIds.insert(type.id.value).second);
    CHECK(names.insert(type.name).second);
    for (const auto& field : type.fields) {
      INFO(std::string{field.name});
      CHECK(fieldIds.insert(field.id.value).second);
      ++fieldCount;
    }
  }
  CHECK(fieldIds.size() == fieldCount);
}

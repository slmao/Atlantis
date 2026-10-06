#include <atlantis/world/ecs/entity_guid_map.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/assert.h>
#include <atlantis/world/ecs/world.h>
#include <atlantis/world/ecs/world_components.h>

// Plan 0050 M5 (Spec 0050 R2; ruling Q4 (G1), (P-a); J1): EntityGuid binds to
// EntityId only at creation, by a caller-owned snapshot; the ECS stores no
// GUID (ADR-0097 D5).

namespace {

namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::EntityGuid;
using atlantis::world::Transform;

[[nodiscard]] EntityGuid guid(std::uint8_t n) {
  atlantis::asset_system::GuidBytes bytes{};
  bytes[0] = std::byte{0xe0};
  bytes[15] = std::byte{n};
  return atlantis::asset_system::entityGuidFromBytes(bytes).value();
}

struct RecordAssertions {
  std::vector<std::string> messages;
  atlantis::AssertFailureHandler previous;
  RecordAssertions() {
    previous = atlantis::assertions::setFailureHandler(
        [this](const atlantis::AssertFailureInfo& info) { messages.emplace_back(info.message); });
  }
  ~RecordAssertions() { atlantis::assertions::setFailureHandler(std::move(previous)); }
  RecordAssertions(const RecordAssertions&) = delete;
  RecordAssertions& operator=(const RecordAssertions&) = delete;
};

}  // namespace

TEST_CASE("ecs guid binding: each GUID gets a fresh entity, in order", "[world][ecs][guid]") {
  ecs::World world;
  const std::array<EntityGuid, 3> guids{guid(3), guid(1), guid(2)};
  const auto map = ecs::createEntities(world, guids);
  REQUIRE(map.isOk());
  CHECK(map.value().size() == 3);
  for (std::uint32_t i = 0; i < guids.size(); ++i) {
    const auto id = map.value().find(guids[i]);
    REQUIRE(id.has_value());
    CHECK(world.isValid(*id));
    CHECK(id->index() == i);  // span order
    CHECK_FALSE(world.has<Transform>(*id).value());
  }
  CHECK_FALSE(map.value().find(guid(9)).has_value());
}

TEST_CASE("ecs guid binding: nil or repeated GUIDs fail before anything is created", "[world][ecs][guid]") {
  ecs::World world;
  const std::array<EntityGuid, 2> withNil{guid(1), EntityGuid{}};
  const auto nil = ecs::createEntities(world, withNil);
  REQUIRE(nil.isErr());
  CHECK(nil.error() == ecs::EcsError::NilGuid);

  const std::array<EntityGuid, 3> repeated{guid(1), guid(2), guid(1)};
  const auto duplicate = ecs::createEntities(world, repeated);
  REQUIRE(duplicate.isErr());
  CHECK(duplicate.error() == ecs::EcsError::DuplicateGuid);

  // Nothing was created: the first new entity still gets index 0.
  CHECK(world.createEntity().index() == 0);
}

TEST_CASE("ecs guid binding: the snapshot does not track liveness", "[world][ecs][guid]") {
  ecs::World world;
  const std::array<EntityGuid, 1> guids{guid(5)};
  const auto map = ecs::createEntities(world, guids);
  REQUIRE(map.isOk());
  const ecs::EntityId id = *map.value().find(guid(5));
  REQUIRE(world.destroyEntity(id).isOk());
  CHECK(map.value().find(guid(5)) == id);  // still the old handle
  CHECK_FALSE(world.isValid(id));          // which the World rejects
}

TEST_CASE("ecs guid binding: refused during a query (J1)", "[world][ecs][guid]") {
  ecs::World world;
  REQUIRE(world.add<Transform>(world.createEntity()).isOk());
  const std::array<EntityGuid, 1> guids{guid(1)};
  RecordAssertions recorded;
  world.query<Transform>([&](ecs::EntityId, Transform&) {
    const auto map = ecs::createEntities(world, guids);
    REQUIRE(map.isErr());
    CHECK(map.error() == ecs::EcsError::StructuralChangeDuringQuery);
  });
#if !defined(NDEBUG)
  CHECK(recorded.messages.size() == 1);
#endif
  CHECK(ecs::createEntities(world, guids).isOk());
}

// Plan 0052 P7 (Spec 0052 ruling Q1): the read-only enumeration the Runtime
// World's operation boundary seeds its index from -- every binding, sorted by
// GUID, each equal to find().
TEST_CASE("ecs guid binding: entries() lists every binding, sorted by GUID", "[world][ecs][guid]") {
  ecs::World world;
  const std::array<EntityGuid, 3> guids{guid(9), guid(2), guid(5)};
  const auto map = ecs::createEntities(world, guids);
  REQUIRE(map.isOk());
  const auto entries = map.value().entries();
  REQUIRE(entries.size() == guids.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    INFO("entry " << i);
    if (i > 0) CHECK(entries[i - 1].first < entries[i].first);
    CHECK(map.value().find(entries[i].first) == entries[i].second);
  }
}

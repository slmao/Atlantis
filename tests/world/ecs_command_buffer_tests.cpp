#include <atlantis/world/ecs/command_buffer.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/world/ecs/world.h>
#include <atlantis/world/ecs/world_components.h>

// Plan 0050 M4 (Spec 0050 R8; rulings J1, J3): deferred structural changes,
// applied in recording order, failures reported per command, the buffer
// cleared, and nothing applied while a query runs.

namespace {

namespace ecs = atlantis::world::ecs;
using atlantis::world::Light;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::Vec3;

[[nodiscard]] Transform at(float x) {
  Transform t;
  t.localPosition = Vec3{x, 0.0f, 0.0f};
  return t;
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

TEST_CASE("ecs command buffer: every command kind applies in recording order", "[world][ecs][command_buffer]") {
  ecs::World world;
  const ecs::EntityId existing = world.createEntity();
  REQUIRE(world.add<Transform>(existing, at(1.0f)).isOk());
  REQUIRE(world.add<Light>(existing).isOk());
  const ecs::EntityId doomed = world.createEntity();

  ecs::CommandBuffer buffer;
  const ecs::PendingEntity made = buffer.create();
  buffer.add<Transform>(made, at(7.0f));
  buffer.add<Renderable>(made, Renderable{11, std::nullopt});
  buffer.set<Transform>(made, at(8.0f));  // after the add, so it applies
  buffer.set<Transform>(existing, at(2.0f));
  buffer.remove<Light>(existing);
  buffer.destroy(doomed);
  CHECK(buffer.size() == 7);

  const ecs::ApplyReport report = buffer.apply(world);
  CHECK(report.failures.empty());
  REQUIRE(report.created.size() == 1);
  const ecs::EntityId createdId = report.created[0];
  CHECK(world.isValid(createdId));
  CHECK(world.get<Transform>(createdId).value().localPosition.x == 8.0f);
  CHECK(world.get<Renderable>(createdId).value().meshAsset == 11);
  CHECK(world.get<Transform>(existing).value().localPosition.x == 2.0f);
  CHECK_FALSE(world.has<Light>(existing).value());
  CHECK_FALSE(world.isValid(doomed));
  CHECK(buffer.empty());
}

TEST_CASE("ecs command buffer: a failed command is reported and later commands still apply (J3)",
          "[world][ecs][command_buffer]") {
  ecs::World world;
  const ecs::EntityId gone = world.createEntity();
  const ecs::EntityId live = world.createEntity();
  REQUIRE(world.destroyEntity(gone).isOk());

  ecs::CommandBuffer buffer;
  buffer.add<Transform>(gone);                // 0: stale target
  buffer.add<Transform>(live, at(3.0f));      // 1
  buffer.add<Transform>(live, at(4.0f));      // 2: already present
  buffer.remove<Renderable>(live);            // 3: missing
  buffer.set<Transform>(live, at(5.0f));      // 4

  const ecs::ApplyReport report = buffer.apply(world);
  REQUIRE(report.failures.size() == 3);
  CHECK(report.failures[0].commandIndex == 0);
  CHECK(report.failures[0].error == ecs::EcsError::InvalidEntity);
  CHECK(report.failures[1].commandIndex == 2);
  CHECK(report.failures[1].error == ecs::EcsError::ComponentAlreadyPresent);
  CHECK(report.failures[2].commandIndex == 3);
  CHECK(report.failures[2].error == ecs::EcsError::ComponentMissing);
  CHECK(world.get<Transform>(live).value().localPosition.x == 5.0f);
  CHECK(buffer.empty());
}

TEST_CASE("ecs command buffer: apply during a query applies nothing and keeps the buffer (J1)",
          "[world][ecs][command_buffer]") {
  ecs::World world;
  const ecs::EntityId e = world.createEntity();
  REQUIRE(world.add<Transform>(e).isOk());

  ecs::CommandBuffer buffer;
  RecordAssertions recorded;
  world.query<Transform>([&](ecs::EntityId id, Transform&) {
    buffer.add<Renderable>(id);  // recording is always allowed
    const ecs::ApplyReport report = buffer.apply(world);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures[0].error == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(report.created.empty());
  });
#if !defined(NDEBUG)
  CHECK(recorded.messages.size() == 1);
#endif
  CHECK_FALSE(world.has<Renderable>(e).value());
  REQUIRE(buffer.size() == 1);

  const ecs::ApplyReport later = buffer.apply(world);  // after the query, it applies
  CHECK(later.failures.empty());
  CHECK(world.has<Renderable>(e).value());
}

TEST_CASE("ecs command buffer: a PendingEntity from another buffer is rejected", "[world][ecs][command_buffer]") {
  ecs::World world;
  ecs::CommandBuffer first;
  ecs::CommandBuffer second;
  const ecs::PendingEntity foreign = first.create();
  second.add<Transform>(foreign);

  RecordAssertions recorded;
  const ecs::ApplyReport report = second.apply(world);
  REQUIRE(report.failures.size() == 1);
  CHECK(report.failures[0].error == ecs::EcsError::InvalidEntity);
#if !defined(NDEBUG)
  CHECK(recorded.messages.size() == 1);
#endif
}

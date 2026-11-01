#include <atlantis/world/ecs/world.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/world/ecs/world_components.h>

#include "ecs_test_access.h"

// Plan 0050 M3 (Spec 0050 R7; rulings Q6, J1, J8): all-of matching, the
// deterministic order, access through the callback's references, and the
// refusal of structural changes while a query runs.

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

// Records failed assertions instead of aborting, for the test's lifetime.
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

struct ReadTransform {
  void operator()(ecs::EntityId, const Transform&) const;
};
struct WriteTransform {
  void operator()(ecs::EntityId, Transform&) const;
};
struct ReadPair {
  void operator()(ecs::EntityId, const Transform&, const Renderable&) const;
};
struct ReadId {
  void operator()(ecs::EntityId) const;
};
struct WriteId {
  void operator()(ecs::EntityId&) const;
};
struct RvalueOnlyRead {
  void operator()(ecs::EntityId, const Transform&) &&;
};

template <typename W, typename Fn, typename... Ts>
concept CanQuery = requires(W& world, Fn& fn) { world.template query<Ts...>(fn); };

template <typename W, typename Fn, typename... Ts>
concept CanReadOnlyQuery = requires(W& world, Fn& fn) { world.template queryReadOnly<Ts...>(fn); };

static_assert(CanReadOnlyQuery<const ecs::World, ReadTransform, Transform>);
static_assert(CanReadOnlyQuery<const ecs::World, ReadTransform, const Transform>);
static_assert(CanReadOnlyQuery<const ecs::World, ReadPair, const Transform, Renderable>);
static_assert(CanReadOnlyQuery<const ecs::World, ReadId>);
static_assert(CanReadOnlyQuery<ecs::World, ReadTransform, Transform>);
static_assert(!CanReadOnlyQuery<const ecs::World, WriteTransform, Transform>);
static_assert(!CanReadOnlyQuery<const ecs::World, WriteTransform, const Transform>);
static_assert(!CanReadOnlyQuery<const ecs::World, WriteId>);
static_assert(!CanReadOnlyQuery<const ecs::World, RvalueOnlyRead, Transform>);
static_assert(!CanReadOnlyQuery<const ecs::World, decltype(&ecs::EntityId::index)>);
static_assert(!CanReadOnlyQuery<const ecs::World, ReadTransform, int>);
static_assert(!CanReadOnlyQuery<const ecs::World, ReadTransform, Transform&>);
static_assert(!CanReadOnlyQuery<const ecs::World, ReadTransform, volatile Transform>);
static_assert(!CanQuery<const ecs::World, ReadTransform, Transform>);
static_assert(!CanQuery<const ecs::World, ReadTransform, const Transform>);
static_assert(!CanQuery<const ecs::World, ReadId>);
static_assert(CanQuery<ecs::World, WriteTransform, Transform>);

[[nodiscard]] ecs::World populatedWorld() {
  ecs::World world;
  for (float x : {1.0f, 2.0f}) {
    const auto id = world.createEntity();
    REQUIRE(world.add<Transform>(id, at(x)).isOk());
  }
  const auto id = world.createEntity();
  REQUIRE(world.add<Transform>(id, at(3.0f)).isOk());
  REQUIRE(world.add<Renderable>(id).isOk());
  return world;
}

}  // namespace

TEST_CASE("ecs const query: actually const populated worlds support const and mixed terms", "[world][ecs][query][const_query]") {
  const ecs::World world = populatedWorld();
  float sum = 0;
  world.queryReadOnly<Transform>([&](ecs::EntityId id, const Transform& t) {
    CHECK(world.isValid(id));
    CHECK(world.has<Transform>(id).value());
    CHECK(world.get<Transform>(id).value().localPosition.x == t.localPosition.x);
    sum += t.localPosition.x;
  });
  CHECK(sum == 6.0f);
  std::size_t pairs = 0;
  world.queryReadOnly<Transform, const Renderable>([&](ecs::EntityId, auto& t, auto& r) {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(t)>>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(r)>>);
    CHECK(t.localPosition.x == 3.0f);
    ++pairs;
  });
  CHECK(pairs == 1);
  std::size_t entities = 0;
  world.queryReadOnly<>([&](ecs::EntityId) { ++entities; });
  CHECK(entities == 3);
  std::size_t lights = 0;
  world.queryReadOnly<const Light>([&](ecs::EntityId, const Light&) { ++lights; });
  CHECK(lights == 0);
  const ecs::World empty;
  empty.queryReadOnly<>([](ecs::EntityId) { FAIL("empty World must not visit a row"); });
  empty.queryReadOnly<Transform>([](ecs::EntityId, const Transform&) { FAIL("empty World must not match"); });
}

TEST_CASE("ecs const query: matching order and live addresses equal the mutable walk across chunks",
          "[world][ecs][query][const_query]") {
  ecs::World world;
  const auto probe = world.createEntity();
  REQUIRE(world.add<Transform>(probe, at(0)).isOk());
  const auto capacity = ecs::EcsTestAccess::capacity(world, {ecs::componentTypeId<Transform>()});
  for (std::uint32_t i = 1; i < capacity + 3; ++i) {
    const auto id = world.createEntity();
    REQUIRE(world.add<Transform>(id, at(static_cast<float>(i))).isOk());
  }
  const auto lit = world.createEntity();
  REQUIRE(world.add<Transform>(lit, at(-1)).isOk());
  REQUIRE(world.add<Light>(lit).isOk());
  const auto drawn = world.createEntity();
  REQUIRE(world.add<Transform>(drawn, at(-2)).isOk());
  REQUIRE(world.add<Renderable>(drawn).isOk());
  [[maybe_unused]] const auto noComponents = world.createEntity();
  REQUIRE(ecs::EcsTestAccess::chunkCount(world, {ecs::componentTypeId<Transform>()}) == 2);

  struct Sample {
    ecs::EntityId id;
    std::uintptr_t address;  // compare addresses as values, never retain borrowed references
    float x;
    bool operator==(const Sample&) const = default;
  };
  std::vector<Sample> expected;
  world.query<Transform>([&](ecs::EntityId id, Transform& t) {
    expected.push_back({id, reinterpret_cast<std::uintptr_t>(&t), t.localPosition.x});
  });
  const ecs::World& readOnly = world;
  for (int repeat = 0; repeat < 2; ++repeat) {
    std::vector<Sample> actual;
    readOnly.queryReadOnly<Transform>([&](ecs::EntityId id, const Transform& t) {
      actual.push_back({id, reinterpret_cast<std::uintptr_t>(&t), t.localPosition.x});
    });
    CHECK(actual == expected);
  }
  std::vector<Sample> explicitConst;
  std::as_const(world).queryReadOnly<const Transform>([&](ecs::EntityId id, const Transform& t) {
    explicitConst.push_back({id, reinterpret_cast<std::uintptr_t>(&t), t.localPosition.x});
  });
  CHECK(explicitConst == expected);
  std::vector<ecs::EntityId> mutableIds, constIds;
  world.query<>([&](ecs::EntityId id) { mutableIds.push_back(id); });
  readOnly.queryReadOnly<>([&](ecs::EntityId id) { constIds.push_back(id); });
  CHECK(constIds == mutableIds);
  CHECK(constIds.size() == expected.size() + 1);
  readOnly.queryReadOnly<Transform, const Light>([&](ecs::EntityId id, const Transform& t, const Light&) {
    CHECK(id == lit);
    CHECK(t.localPosition.x == -1);
  });
}

TEST_CASE("ecs const query: invokes a move-only lvalue callable without moving or copying it",
          "[world][ecs][query][const_query]") {
  struct Reader {
    std::unique_ptr<int> calls = std::make_unique<int>(0);
    int operator()(ecs::EntityId, const Transform&) & { return ++*calls; }
  } reader;
  static_assert(!std::is_copy_constructible_v<Reader>);
  static_assert(CanReadOnlyQuery<const ecs::World, Reader, Transform>);
  const ecs::World world = populatedWorld();
  const int* identity = reader.calls.get();
  world.queryReadOnly<Transform>(reader);
  world.queryReadOnly<const Transform>(std::move(reader));
  REQUIRE(reader.calls.get() == identity);
  CHECK(*reader.calls == 6);
}

TEST_CASE("ecs const query: shared guard refuses structural aliases and releases after all exits",
          "[world][ecs][query][const_query]") {
  ecs::World world;
  const auto id = world.createEntity();
  REQUIRE(world.add<Transform>(id, at(1)).isOk());
  const ecs::World& readOnly = world;
  RecordAssertions recorded;
  readOnly.queryReadOnly<Transform>([&](ecs::EntityId current, const Transform& t) {
    CHECK(world.add<Renderable>(current).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.remove<Transform>(current).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.destroyEntity(current).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.createEntity() == ecs::kInvalidEntityId);
    CHECK(t.localPosition.x == 1);
    CHECK(world.has<Transform>(current).value());
  });
#if !defined(NDEBUG)
  CHECK(recorded.messages.size() == 4);
#else
  CHECK(recorded.messages.empty());
#endif
  CHECK(world.isValid(id));
  CHECK_FALSE(world.has<Renderable>(id).value());
  REQUIRE(world.add<Renderable>(id).isOk());
  REQUIRE(world.remove<Renderable>(id).isOk());
  readOnly.queryReadOnly<Light>([](ecs::EntityId, const Light&) { FAIL("no matching Light"); });
  REQUIRE(world.add<Renderable>(id).isOk());  // no-match walk balanced the depth too
  REQUIRE(world.remove<Renderable>(id).isOk());

  struct Interrupted {};
  CHECK_THROWS_AS(readOnly.queryReadOnly<Transform>([](ecs::EntityId, const Transform&) { throw Interrupted{}; }), Interrupted);
  REQUIRE(world.add<Renderable>(id).isOk());  // test-owned unwinding left no active query
}

TEST_CASE("ecs const query: existing generic mutable callbacks keep writable references",
          "[world][ecs][query][const_query]") {
  ecs::World world = populatedWorld();
  world.query<Transform>([](ecs::EntityId, auto& t) { t.localPosition.x *= 2.0f; });
  float sum = 0;
  world.query<const Transform>([&](ecs::EntityId, const auto& t) { sum += t.localPosition.x; });
  CHECK(sum == 12.0f);
}

TEST_CASE("ecs query: legacy generic id categories survive all-const and empty queries",
          "[world][ecs][query][const_query]") {
  ecs::World world = populatedWorld();
  std::size_t components = 0;
  world.query<const Transform>([&](auto&& id, const auto& t) {
    static_assert(std::is_same_v<decltype(id), const ecs::EntityId&>);
    static_assert(std::is_same_v<decltype(t), const Transform&>);
    CHECK(world.get<Transform>(id).value().localPosition.x == t.localPosition.x);
    ++components;
  });
  std::size_t entities = 0;
  world.query<>([&](auto&& id) {
    static_assert(std::is_same_v<decltype(id), const ecs::EntityId&>);
    CHECK(world.isValid(id));
    ++entities;
  });
  CHECK(components == 3);
  CHECK(entities == 3);
}

TEST_CASE("ecs query: legacy receivers and lvalue overloads remain independent of read-only access",
          "[world][ecs][query][const_query]") {
  struct Callback {
    std::unique_ptr<int> writable = std::make_unique<int>(0);
    int oldRead = 0;
    int newRead = 0;
    void operator()(const ecs::EntityId&, Transform& t) & {
      ++*writable;
      t.localPosition.x += 1;
    }
    void operator()(const ecs::EntityId&, const Transform&) & { ++oldRead; }
    void operator()(ecs::EntityId&&, const Transform&) & { ++newRead; }
  } callback;
  static_assert(!std::is_copy_constructible_v<Callback>);
  ecs::World world = populatedWorld();
  const int* identity = callback.writable.get();
  world.query<Transform>(callback);
  world.query<const Transform>(std::move(callback));
  std::move(world).query<Transform>(callback);  // original unqualified receiver accepts rvalues
  world.queryReadOnly<Transform>(callback);
  std::as_const(world).queryReadOnly<const Transform>(callback);
  REQUIRE(callback.writable.get() == identity);
  CHECK(*callback.writable == 6);
  CHECK(callback.oldRead == 3);
  CHECK(callback.newRead == 6);
  float sum = 0;
  world.query<const Transform>([&](ecs::EntityId, const auto& t) { sum += t.localPosition.x; });
  CHECK(sum == 12.0f);
  std::size_t mixed = 0;
  world.query<Transform, const Renderable>([&](auto&& id, auto& t, auto& r) {
    static_assert(std::is_same_v<decltype(id), const ecs::EntityId&>);
    static_assert(std::is_same_v<decltype(t), Transform&>);
    static_assert(std::is_same_v<decltype(r), const Renderable&>);
    ++mixed;
  });
  CHECK(mixed == 1);
}

TEST_CASE("ecs query: matches every archetype that contains all requested types", "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId transformOnly = world.createEntity();
  const ecs::EntityId both = world.createEntity();
  const ecs::EntityId renderableOnly = world.createEntity();
  const ecs::EntityId all = world.createEntity();
  REQUIRE(world.add<Transform>(transformOnly).isOk());
  REQUIRE(world.add<Transform>(both).isOk());
  REQUIRE(world.add<Renderable>(both).isOk());
  REQUIRE(world.add<Renderable>(renderableOnly).isOk());
  REQUIRE(world.add<Transform>(all).isOk());
  REQUIRE(world.add<Renderable>(all).isOk());
  REQUIRE(world.add<Light>(all).isOk());

  std::vector<ecs::EntityId> seen;
  world.query<Transform, Renderable>([&](ecs::EntityId e, Transform&, Renderable&) { seen.push_back(e); });
  CHECK(seen == std::vector<ecs::EntityId>{both, all});

  std::size_t everything = 0;
  world.query<>([&](ecs::EntityId) { ++everything; });
  CHECK(everything == 4);
}

TEST_CASE("ecs query: order is archetype creation, then chunk, then row", "[world][ecs][query]") {
  ecs::World world;
  // Archetype {T} first, filled past one chunk; then {T,L}; then {T,R}.
  std::vector<ecs::EntityId> expected;
  const std::uint32_t capacity = [&] {
    const ecs::EntityId probe = world.createEntity();
    REQUIRE(world.add<Transform>(probe).isOk());
    expected.push_back(probe);
    return ecs::EcsTestAccess::capacity(world, {ecs::componentTypeId<Transform>()});
  }();
  while (expected.size() < capacity + 3) {
    expected.push_back(world.createEntity());
    REQUIRE(world.add<Transform>(expected.back()).isOk());
  }
  REQUIRE(ecs::EcsTestAccess::chunkCount(world, {ecs::componentTypeId<Transform>()}) == 2);
  const ecs::EntityId lit = world.createEntity();
  REQUIRE(world.add<Transform>(lit).isOk());  // passes through {T} briefly, then moves on
  REQUIRE(world.add<Light>(lit).isOk());
  const ecs::EntityId drawn = world.createEntity();
  REQUIRE(world.add<Transform>(drawn).isOk());
  REQUIRE(world.add<Renderable>(drawn).isOk());
  expected.push_back(lit);
  expected.push_back(drawn);

  std::vector<ecs::EntityId> seen;
  world.query<Transform>([&](ecs::EntityId e, Transform&) { seen.push_back(e); });
  CHECK(seen == expected);

  std::vector<ecs::EntityId> again;
  world.query<const Transform>([&](ecs::EntityId e, const Transform&) { again.push_back(e); });
  CHECK(again == seen);  // repeatable
}

TEST_CASE("ecs query: mutation through the references is visible to get", "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  const ecs::EntityId b = world.createEntity();
  REQUIRE(world.add<Transform>(a, at(1.0f)).isOk());
  REQUIRE(world.add<Transform>(b, at(2.0f)).isOk());
  world.query<Transform>([](ecs::EntityId, Transform& t) { t.localPosition.x *= 10.0f; });
  CHECK(world.get<Transform>(a).value().localPosition.x == 10.0f);
  CHECK(world.get<Transform>(b).value().localPosition.x == 20.0f);

  float sum = 0.0f;
  world.query<const Transform>([&](ecs::EntityId, const Transform& t) { sum += t.localPosition.x; });
  CHECK(sum == 30.0f);
}

TEST_CASE("ecs query: nested queries and get/set/has inside a callback are allowed (J8)", "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  const ecs::EntityId b = world.createEntity();
  REQUIRE(world.add<Transform>(a, at(1.0f)).isOk());
  REQUIRE(world.add<Transform>(b, at(2.0f)).isOk());
  REQUIRE(world.add<Renderable>(b).isOk());

  std::size_t pairs = 0;
  world.query<const Transform>([&](ecs::EntityId outer, const Transform&) {
    world.query<const Transform>([&](ecs::EntityId, const Transform&) { ++pairs; });
    CHECK(world.has<Transform>(outer).value());
    REQUIRE(world.set<Transform>(b, at(5.0f)).isOk());  // another entity, no row moves
    CHECK(world.get<Transform>(b).value().localPosition.x == 5.0f);
  });
  CHECK(pairs == 4);
}

TEST_CASE("ecs query: structural changes inside a callback are refused and change nothing (J1)",
          "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  REQUIRE(world.add<Transform>(a, at(1.0f)).isOk());
  const ecs::EntityId b = world.createEntity();

  RecordAssertions recorded;
  world.query<Transform>([&](ecs::EntityId e, Transform&) {
    CHECK(world.add<Renderable>(e).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.remove<Transform>(e).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.destroyEntity(b).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.createEntity() == ecs::kInvalidEntityId);
  });

#if !defined(NDEBUG)
  CHECK(recorded.messages.size() == 4);  // ATLANTIS_ASSERT_MSG fires once per refused call (Debug only)
#else
  CHECK(recorded.messages.empty());
#endif
  CHECK(world.isValid(b));
  CHECK_FALSE(world.has<Renderable>(a).value());
  CHECK(world.get<Transform>(a).value().localPosition.x == 1.0f);
  CHECK(ecs::EcsTestAccess::archetypeCount(world) == 2);  // {} and {T}: nothing new
  // After the query, the same operations succeed.
  CHECK(world.add<Renderable>(a).isOk());
}

#include <atlantis/world/access/runtime_world_access.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/schema.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

// Plan 0053 M4 (Spec 0053 R7; ruling Q7; J7): for command sequences drawn
// from fixed seeds, a transaction's verdict -- commit, or abort at its first
// refused command -- equals Spec 0052's one-by-one verdict on an identically
// baked world, with the same events and the same resulting world. Sequences
// are purely random or built only from commands the one-by-one path accepts
// (some with one random command spliced in), each over the plain scene and
// over the scene saturated near the light limits.

namespace {

constexpr std::string_view kTestTag = "world_access_transaction_equivalence_tests";

namespace fs = std::filesystem;
namespace access = atlantis::world::access;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::ValidatedSceneData;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

[[nodiscard]] ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  const fs::path dir = fs::temp_directory_path() / ("atlantis_" + std::string(kTestTag)) /
                        ("fixture_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00520052-0052-4052-8052-005200520052").value(), "scene");
  REQUIRE(atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), guid,
                                            (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// One node per component kind: a material renderable, a Directional and a
// Point light, the active camera, and a bare node.
constexpr const char* kSceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 5\n"
    "active_camera: 4\n"
    "node: node_id=1 guid=52052052-0001-4052-8052-000000000001 parent=none position=1.0 2.0 3.0 "
    "rotation=0.1 0.2 0.3 scale=1.0 2.0 1.0 mesh=52052052-00aa-4052-8052-0000000000aa "
    "material=52052052-00bb-4052-8052-0000000000bb\n"
    "node: node_id=2 guid=52052052-0002-4052-8052-000000000002 parent=none position=0.0 5.0 0.0 "
    "rotation=0.5 -0.6 0.0 scale=1.0 1.0 1.0 light=directional color=0.6 0.7 1.0 intensity=1.2\n"
    "node: node_id=3 guid=52052052-0003-4052-8052-000000000003 parent=1 position=0.8 0.3 0.5 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=1.0 0.6 0.3 intensity=3.0 range=2.5\n"
    "node: node_id=4 guid=52052052-0004-4052-8052-000000000004 parent=none position=0.0 2.2 7.0 "
    "rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n"
    "node: node_id=5 guid=52052052-0005-4052-8052-000000000005 parent=none position=0.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";

[[maybe_unused]] constexpr std::size_t kRenderableNode = 0;
[[maybe_unused]] constexpr std::size_t kDirectionalNode = 1;
[[maybe_unused]] constexpr std::size_t kPointNode = 2;
[[maybe_unused]] constexpr std::size_t kCameraNode = 3;
[[maybe_unused]] constexpr std::size_t kBareNode = 4;

[[maybe_unused]] [[nodiscard]] EntityGuid newGuid(std::uint32_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x52};
  guid.bytes[12] = static_cast<std::byte>((n >> 24) & 0xff);
  guid.bytes[13] = static_cast<std::byte>((n >> 16) & 0xff);
  guid.bytes[14] = static_cast<std::byte>((n >> 8) & 0xff);
  guid.bytes[15] = static_cast<std::byte>(n & 0xff);
  return guid;
}

template <typename T>
[[nodiscard]] atlantis::schema::TypeId typeOf() {
  return ecs::componentTypeId<T>();
}

[[maybe_unused]] [[nodiscard]] atlantis::schema::FieldId field(std::string_view owner, std::string_view name) {
  return atlantis::schema::fieldId(owner, name);
}

[[maybe_unused]] [[nodiscard]] access::AccessError refusal(access::RuntimeWorldAccess& boundary, access::Command command) {
  const access::CommandTicket ticket = boundary.submit(std::move(command));
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.size() == 1);
  CHECK(report.failures[0].ticket == ticket);
  CHECK(report.applied == 0);
  return report.failures[0].error;
}

[[nodiscard]] access::SetProperty set(const EntityGuid& entity, atlantis::schema::TypeId component,
                                      atlantis::schema::FieldId fieldId, access::PropertyValue value) {
  return access::SetProperty{{entity, component, fieldId}, std::move(value)};
}

// A Point light with a WorldMatrix -- one extraction counts -- built through
// the boundary in the order Correction J1 allows.
void addPointLight(access::RuntimeWorldAccess& boundary, const EntityGuid& g) {
  using namespace atlantis::world;
  boundary.submit(access::CreateEntity{g});
  boundary.submit(access::AddComponent{g, typeOf<Light>()});
  boundary.submit(set(g, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{1}));
  boundary.submit(access::AddComponent{g, typeOf<WorldMatrix>()});
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.empty());
}

}  // namespace

namespace {

using atlantis::schema::FieldDescriptor;
using atlantis::schema::FieldFlags;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeId;
using atlantis::schema::TypeKind;

// SplitMix64 (Steele, Lea, Flood 2014): written out here so a sequence
// depends only on its seed, never on a standard library's distributions.
class SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
  std::uint64_t next() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
  bool chance(std::uint32_t percent) { return below(100) < percent; }

 private:
  std::uint64_t state_;
};

[[nodiscard]] const TypeDescriptor& requireType(TypeId id) {
  for (const TypeDescriptor& type : atlantis::world::worldSchema()) {
    if (type.id == id) return type;
  }
  FAIL("type not in worldSchema()");
  return atlantis::world::worldSchema()[0];
}

struct Leaf {
  TypeId component;
  const FieldDescriptor* field = nullptr;
};

void collectLeaves(TypeId component, const TypeDescriptor& type, std::vector<Leaf>& out) {
  for (const FieldDescriptor& f : type.fields) {
    if (f.kind == TypeKind::Struct) {
      collectLeaves(component, requireType(f.type), out);
    } else {
      out.push_back({component, &f});
    }
  }
}

[[nodiscard]] const std::vector<Leaf>& allLeaves() {
  static const std::vector<Leaf> leaves = [] {
    std::vector<Leaf> out;
    std::apply([&](auto... tag) { (collectLeaves(typeOf<decltype(tag)>(), requireType(typeOf<decltype(tag)>()), out), ...); },
               ecs::WorldComponentTypes{});
    return out;
  }();
  return leaves;
}

[[nodiscard]] std::vector<TypeId> componentPool() {
  std::vector<TypeId> pool;
  std::apply([&](auto... tag) { (pool.push_back(typeOf<decltype(tag)>()), ...); }, ecs::WorldComponentTypes{});
  pool.push_back(atlantis::schema::typeId("world::CameraFog"));  // not a World component
  return pool;
}

[[nodiscard]] float randomFloat(SplitMix64& rng) {
  if (rng.chance(4)) return std::numeric_limits<float>::quiet_NaN();
  if (rng.chance(3)) return -std::numeric_limits<float>::infinity();
  return static_cast<float>(rng.below(2001)) / 100.0f - 10.0f;
}

// A value for `leaf`: usually of its kind, sometimes not.
[[nodiscard]] access::PropertyValue randomValue(SplitMix64& rng, const Leaf& leaf) {
  const FieldDescriptor& f = *leaf.field;
  if (rng.chance(6)) return access::PropertyValue{std::array<float, 3>{1.0f, 2.0f, 3.0f}};  // often a kind mismatch
  if (f.kind == TypeKind::Enum) {
    static constexpr std::int64_t kKinds[] = {0, 1, 1, 7};  // Directional, Point (twice as likely), out of range
    return access::EnumValue{kKinds[rng.below(4)]};
  }
  if (atlantis::schema::hasFlags(f.flags, FieldFlags::Optional) && rng.chance(40)) return access::Absent{};
  switch (f.primitive) {
    case PrimitiveKind::UInt64: return std::uint64_t{rng.next()};
    case PrimitiveKind::Float32: return randomFloat(rng);
    case PrimitiveKind::Vec3Float32: return std::array<float, 3>{randomFloat(rng), randomFloat(rng), randomFloat(rng)};
    case PrimitiveKind::Vec4Float32:
      return std::array<float, 4>{randomFloat(rng), randomFloat(rng), randomFloat(rng), 1.0f};
    case PrimitiveKind::AssetGuid: return atlantis::asset_system::AssetGuid{};
    case PrimitiveKind::EntityGuid: return EntityGuid{};
  }
  return access::Absent{};
}

struct Pools {
  std::vector<EntityGuid> guids;
  std::vector<TypeId> components = componentPool();
};

[[nodiscard]] access::Command randomCommand(SplitMix64& rng, const Pools& pools) {
  const EntityGuid& guid = pools.guids[rng.below(pools.guids.size())];
  const TypeId component = pools.components[rng.below(pools.components.size())];
  switch (rng.below(20)) {
    case 0: case 1: case 2: return access::CreateEntity{rng.chance(10) ? EntityGuid{} : guid};
    case 3: case 4: return access::DestroyEntity{guid};
    case 5: case 6: case 7: case 8: case 9: return access::AddComponent{guid, component};
    case 10: case 11: case 12: return access::RemoveComponent{guid, component};
    default: {
      const auto& leaves = allLeaves();
      const Leaf& leaf = leaves[rng.below(leaves.size())];
      // Now and then the field is addressed under the wrong component.
      const TypeId owner = rng.chance(5) ? component : leaf.component;
      return access::SetProperty{{guid, owner, leaf.field->id}, randomValue(rng, leaf)};
    }
  }
}

// The world every sequence starts from: the baked scene, and, near the light
// limits, extra Point lights (two of them in the GUID pool, so a sequence can
// free a slot).
constexpr std::uint32_t kSaturatingPointLights = access::kMaxPointLights - 2;

struct World {
  atlantis::world::BakedScene baked;
  access::RuntimeWorldAccess boundary;
  explicit World(const ValidatedSceneData& scene, bool nearLimit)
      : baked(atlantis::world::bakeScene(scene)), boundary(baked) {
    if (nearLimit) {
      for (std::uint32_t i = 0; i < kSaturatingPointLights; ++i) addPointLight(boundary, newGuid(100 + i));
    }
    (void)boundary.drainEvents();
  }
};

[[nodiscard]] Pools poolsFor(const ValidatedSceneData& scene) {
  Pools pools;
  for (std::size_t node = 0; node < 5; ++node) pools.guids.push_back(scene.entityGuid(node));
  for (std::uint32_t n = 1; n <= 4; ++n) pools.guids.push_back(newGuid(n));
  pools.guids.push_back(newGuid(100));
  pools.guids.push_back(newGuid(101));
  return pools;
}

struct EntitySnapshot {
  bool live = false;
  std::vector<TypeId> components;
  std::vector<access::PropertyValue> values;
  friend bool operator==(const EntitySnapshot&, const EntitySnapshot&) = default;
};

[[nodiscard]] std::vector<EntitySnapshot> snapshot(const access::RuntimeWorldAccess& boundary, const Pools& pools) {
  std::vector<EntitySnapshot> out;
  for (const EntityGuid& guid : pools.guids) {
    EntitySnapshot entity;
    entity.live = boundary.findEntity(guid);
    if (entity.live) {
      entity.components = boundary.listComponents(guid).value();
      for (const Leaf& leaf : allLeaves()) {
        if (std::find(entity.components.begin(), entity.components.end(), leaf.component) == entity.components.end())
          continue;
        entity.values.push_back(boundary.getProperty({guid, leaf.component, leaf.field->id}).value());
      }
    }
    out.push_back(std::move(entity));
  }
  return out;
}

struct Tally {
  std::size_t commits = 0;
  std::size_t aborts = 0;
  std::size_t lightLimitAborts = 0;
  std::size_t lateAborts = 0;  // the first refusal after position 0
};

// Spec 0053 R7: the transaction's verdict equals Spec 0052's one-by-one
// verdict on an identical world -- commit exactly when no command is refused
// one by one, with the same events and the same world; otherwise an abort at
// the first refused command with its error, and no trace.
void requireEquivalent(const ValidatedSceneData& scene, bool nearLimit, const Pools& pools,
                       const std::vector<access::Command>& commands, Tally& tally) {
  World singly(scene, nearLimit);
  World grouped(scene, nearLimit);

  std::optional<access::CommandTicket> firstTicket;
  for (const auto& command : commands) {
    const access::CommandTicket ticket = singly.boundary.submit(command);
    if (!firstTicket) firstTicket = ticket;
  }
  const access::ApplyReport sequential = singly.boundary.applyPending();
  const std::vector<access::Event> sequentialEvents = singly.boundary.drainEvents();

  const std::vector<EntitySnapshot> before = snapshot(grouped.boundary, pools);
  const access::TransactionTicket ticket = grouped.boundary.submitTransaction(commands);
  const access::ApplyReport report = grouped.boundary.applyPending();
  const std::vector<access::Event> events = grouped.boundary.drainEvents();
  REQUIRE(ticket.count == commands.size());

  if (sequential.failures.empty()) {
    ++tally.commits;
    CHECK(report.failures.empty());
    CHECK(report.applied == commands.size());
    CHECK(events == sequentialEvents);
    CHECK(snapshot(grouped.boundary, pools) == snapshot(singly.boundary, pools));
  } else {
    ++tally.aborts;
    const access::CommandFailure& first = sequential.failures.front();
    const std::uint64_t position = first.ticket.value - firstTicket->value;
    if (first.error == access::AccessError::LightLimitExceeded) ++tally.lightLimitAborts;
    if (position > 0) ++tally.lateAborts;
    CHECK(report.applied == 0);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures[0].ticket.value - ticket.first.value == position);
    CHECK(report.failures[0].error == first.error);
    CHECK(events.empty());
    CHECK(snapshot(grouped.boundary, pools) == before);
  }
}

// A sequence of commands each of which the one-by-one path accepts (checked
// against a scratch world), with, in some, one random command spliced in.
[[nodiscard]] std::vector<access::Command> acceptedSequence(SplitMix64& rng, const ValidatedSceneData& scene,
                                                            bool nearLimit, const Pools& pools) {
  World scratch(scene, nearLimit);
  std::vector<access::Command> commands;
  const std::size_t length = 2 + rng.below(10);
  for (int attempts = 0; commands.size() < length && attempts < 400; ++attempts) {
    access::Command candidate = randomCommand(rng, pools);
    scratch.boundary.submit(candidate);
    if (scratch.boundary.applyPending().failures.empty()) commands.push_back(std::move(candidate));
  }
  if (rng.chance(50)) {
    const std::size_t at = rng.below(commands.size() + 1);
    commands.insert(commands.begin() + static_cast<std::ptrdiff_t>(at), randomCommand(rng, pools));
  }
  return commands;
}

// The fixed seeds: sequence i of a kind uses kSeedBase + kind * 1000 + i.
constexpr std::uint64_t kSeedBase = 0x0053'0000'0000ull;
constexpr std::uint64_t kSequencesPerKind = 150;

}  // namespace

TEST_CASE("world access transactions: the transaction verdict equals the one-by-one verdict (fixed seeds)",
          "[world][access][transaction]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  const Pools pools = poolsFor(scene);
  Tally tally;

  for (const bool nearLimit : {false, true}) {
    for (std::uint64_t i = 0; i < kSequencesPerKind; ++i) {
      const std::uint64_t seed = kSeedBase + (nearLimit ? 2000 : 0) + i;
      INFO("random sequence, seed " << seed << (nearLimit ? ", near the light limits" : ""));
      SplitMix64 rng(seed);
      std::vector<access::Command> commands;
      const std::size_t length = 1 + rng.below(12);
      for (std::size_t n = 0; n < length; ++n) commands.push_back(randomCommand(rng, pools));
      requireEquivalent(scene, nearLimit, pools, commands, tally);
    }
    for (std::uint64_t i = 0; i < kSequencesPerKind; ++i) {
      const std::uint64_t seed = kSeedBase + 1000 + (nearLimit ? 2000 : 0) + i;
      INFO("accepted sequence, seed " << seed << (nearLimit ? ", near the light limits" : ""));
      SplitMix64 rng(seed);
      requireEquivalent(scene, nearLimit, pools, acceptedSequence(rng, scene, nearLimit, pools), tally);
    }
  }

  // Not vacuous: both verdicts occur, refusals occur past the first command,
  // and the light limits are reached.
  INFO("commits " << tally.commits << ", aborts " << tally.aborts << " (late " << tally.lateAborts
                  << ", light limit " << tally.lightLimitAborts << ")");
  CHECK(tally.commits >= 100);
  CHECK(tally.aborts >= 100);
  CHECK(tally.lateAborts >= 50);
  CHECK(tally.lightLimitAborts >= 5);
}

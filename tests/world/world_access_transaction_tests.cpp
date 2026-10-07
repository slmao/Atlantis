#include <atlantis/world/access/runtime_world_access.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/schema.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Plan 0053 M3 (Spec 0053 R1-R5, R8; rulings Q1-Q4): transactions -- all or
// nothing over Spec 0052's five commands. A committed transaction equals its
// commands applied one by one, with their events; an aborted one leaves no
// trace but one failure at its first refused command; the light limits and
// the active camera hold at every step; ruling Q4's special cases; and
// transactions and single commands apply in submission order.

namespace {

constexpr std::string_view kTestTag = "world_access_transaction_tests";

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
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeId;

[[nodiscard]] const TypeDescriptor& requireType(TypeId id) {
  for (const TypeDescriptor& type : atlantis::world::worldSchema()) {
    if (type.id == id) return type;
  }
  FAIL("type not in worldSchema()");
  return atlantis::world::worldSchema()[0];
}

void collectLeaves(const TypeDescriptor& type, std::vector<atlantis::schema::FieldId>& out) {
  for (const FieldDescriptor& f : type.fields) {
    if (f.kind == atlantis::schema::TypeKind::Struct) {
      collectLeaves(requireType(f.type), out);
    } else {
      out.push_back(f.id);
    }
  }
}

// One entity as a client sees it through the boundary: whether it exists,
// its components, and every leaf field of each (walked from worldSchema()).
struct EntitySnapshot {
  bool live = false;
  std::vector<TypeId> components;
  std::vector<access::PropertyValue> values;
  friend bool operator==(const EntitySnapshot&, const EntitySnapshot&) = default;
};
using Snapshot = std::map<EntityGuid, EntitySnapshot>;

[[nodiscard]] Snapshot snapshot(const access::RuntimeWorldAccess& boundary, const std::vector<EntityGuid>& guids) {
  Snapshot out;
  for (const EntityGuid& guid : guids) {
    EntitySnapshot& entity = out[guid];
    entity.live = boundary.findEntity(guid);
    if (!entity.live) continue;
    entity.components = boundary.listComponents(guid).value();
    for (const TypeId component : entity.components) {
      std::vector<atlantis::schema::FieldId> leaves;
      collectLeaves(requireType(component), leaves);
      for (const auto leaf : leaves) entity.values.push_back(boundary.getProperty({guid, component, leaf}).value());
    }
  }
  return out;
}

// The scene's five node GUIDs and the fresh GUIDs these tests create.
[[nodiscard]] std::vector<EntityGuid> trackedGuids(const ValidatedSceneData& scene) {
  std::vector<EntityGuid> guids;
  for (std::size_t node = 0; node < 5; ++node) guids.push_back(scene.entityGuid(node));
  for (std::uint32_t n = 1; n <= 8; ++n) guids.push_back(newGuid(n));
  guids.push_back(newGuid(500));
  guids.push_back(newGuid(900));
  return guids;
}

// `commands` as one transaction: the ticket, the outcome and the events.
struct Outcome {
  access::TransactionTicket ticket;
  access::ApplyReport report;
  std::vector<access::Event> events;
  std::vector<access::CommandFailure> failures;
};

[[nodiscard]] Outcome runTransaction(access::RuntimeWorldAccess& boundary, std::vector<access::Command> commands) {
  Outcome outcome;
  outcome.ticket = boundary.submitTransaction(std::move(commands));
  outcome.report = boundary.applyPending();
  outcome.events = boundary.drainEvents();
  outcome.failures = boundary.drainFailures();
  return outcome;
}

// The same commands submitted one by one (Spec 0052's path).
[[nodiscard]] std::vector<access::Event> runSingly(access::RuntimeWorldAccess& boundary,
                                                   const std::vector<access::Command>& commands) {
  for (const auto& command : commands) boundary.submit(command);
  const auto report = boundary.applyPending();
  CHECK(report.failures.empty());
  return boundary.drainEvents();
}

// Requires an abort at `position` with `error`, and no trace: no event, the
// snapshot unchanged, the transaction's own commands' tickets consumed.
void requireAborted(access::RuntimeWorldAccess& boundary, const std::vector<EntityGuid>& guids,
                    std::vector<access::Command> commands, std::size_t position, access::AccessError error) {
  const Snapshot before = snapshot(boundary, guids);
  const std::size_t count = commands.size();
  const Outcome outcome = runTransaction(boundary, std::move(commands));
  CHECK(outcome.ticket.count == count);
  CHECK(outcome.report.applied == 0);
  REQUIRE(outcome.report.failures.size() == 1);
  REQUIRE(outcome.failures.size() == 1);
  CHECK(outcome.failures[0] == outcome.report.failures[0]);
  CHECK(outcome.ticket.contains(outcome.failures[0].ticket));
  CHECK(outcome.failures[0].ticket.value - outcome.ticket.first.value == position);
  CHECK(outcome.failures[0].error == error);
  CHECK(outcome.events.empty());
  CHECK(snapshot(boundary, guids) == before);
}

[[nodiscard]] std::uint32_t pointLights(atlantis::world::BakedScene& baked) {
  using namespace atlantis::world;
  std::uint32_t points = 0;
  baked.world.query<const Light, const WorldMatrix>([&](ecs::EntityId, const Light& light, const WorldMatrix&) {
    if (light.kind == LightKind::Point) ++points;
  });
  return points;
}

// The Motivation's group: a Point light with its placement.
[[nodiscard]] std::vector<access::Command> pointLightGroup(const EntityGuid& g) {
  using namespace atlantis::world;
  return {access::CreateEntity{g}, access::AddComponent{g, typeOf<Light>()},
          set(g, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{1}),
          access::AddComponent{g, typeOf<WorldMatrix>()},
          set(g, typeOf<WorldMatrix>(), field("world::WorldMatrix", "column3"),
              std::array<float, 4>{1.0f, 2.0f, 3.0f, 1.0f})};
}

}  // namespace

TEST_CASE("world access transactions: a committed transaction equals its commands applied one by one",
          "[world][access][transaction]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  BakedScene bakedA = bakeScene(scene);
  BakedScene bakedB = bakeScene(scene);
  access::RuntimeWorldAccess singly(bakedA);
  access::RuntimeWorldAccess grouped(bakedB);
  const std::vector<EntityGuid> guids = trackedGuids(scene);
  const EntityGuid g = newGuid(1);

  // Every command kind, with the Point-light group first.
  std::vector<access::Command> commands = pointLightGroup(g);
  commands.push_back(access::AddComponent{g, typeOf<Transform>()});
  commands.push_back(set(g, typeOf<Transform>(), field("world::Transform", "localPosition"),
                         std::array<float, 3>{4.0f, 5.0f, 6.0f}));
  commands.push_back(access::RemoveComponent{g, typeOf<Transform>()});
  commands.push_back(set(scene.entityGuid(kRenderableNode), typeOf<Renderable>(),
                         field("world::Renderable", "materialAsset"), access::Absent{}));
  commands.push_back(access::DestroyEntity{scene.entityGuid(kBareNode)});

  const std::vector<access::Event> expected = runSingly(singly, commands);
  REQUIRE(expected.size() == commands.size());
  const Outcome outcome = runTransaction(grouped, commands);
  CHECK(outcome.ticket == access::TransactionTicket{access::CommandTicket{1}, commands.size()});
  CHECK(outcome.report.applied == commands.size());
  CHECK(outcome.report.failures.empty());
  CHECK(outcome.failures.empty());
  CHECK(outcome.events == expected);  // one per command, in order; no transaction event
  CHECK(snapshot(grouped, guids) == snapshot(singly, guids));
  CHECK(pointLights(bakedB) == 2);
}

TEST_CASE("world access transactions: a refusal anywhere aborts the whole transaction and leaves no trace",
          "[world][access][transaction]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  const std::vector<EntityGuid> guids = trackedGuids(scene);
  const EntityGuid bare = scene.entityGuid(kBareNode);
  const EntityGuid point = scene.entityGuid(kPointNode);
  const EntityGuid fresh = newGuid(500);
  const float nan = std::numeric_limits<float>::quiet_NaN();

  // Every refusal a command can meet through the boundary. FieldNotEditable
  // cannot: every World field is Editable (Plans 0048 J3, 0051 J3).
  const std::vector<std::pair<access::Command, access::AccessError>> refused = {
      {access::CreateEntity{EntityGuid{}}, access::AccessError::NilGuid},
      {access::CreateEntity{bare}, access::AccessError::DuplicateGuid},
      {access::DestroyEntity{newGuid(900)}, access::AccessError::UnknownEntity},
      {access::AddComponent{bare, atlantis::schema::typeId("world::CameraFog")},
       access::AccessError::UnknownComponentType},
      {access::RemoveComponent{bare, typeOf<Light>()}, access::AccessError::ComponentMissing},
      {access::AddComponent{bare, typeOf<Transform>()}, access::AccessError::ComponentAlreadyPresent},
      {set(bare, typeOf<Transform>(), field("world::Light", "range"), 1.0f), access::AccessError::UnknownField},
      {set(point, typeOf<Light>(), field("world::Light", "intensity"), std::array<float, 3>{}),
       access::AccessError::KindMismatch},
      {set(point, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{7}),
       access::AccessError::EnumValueOutOfRange},
      {set(point, typeOf<Light>(), field("world::Light", "intensity"), nan), access::AccessError::NonFiniteValue},
      {set(point, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{0}),
       access::AccessError::LightLimitExceeded},
      {access::DestroyEntity{scene.entityGuid(kCameraNode)}, access::AccessError::ActiveCameraProtected},
  };
  // Accepted commands around the refused one: an edit, a new entity, a component on it.
  const std::vector<access::Command> accepted = {
      set(scene.entityGuid(kDirectionalNode), typeOf<Light>(), field("world::Light", "intensity"), 2.0f),
      access::CreateEntity{fresh}, access::AddComponent{fresh, typeOf<Transform>()}};

  for (const auto& [command, error] : refused) {
    for (const std::size_t position : {std::size_t{0}, std::size_t{2}, std::size_t{3}}) {
      INFO("error " << access::toString(error) << " at position " << position);
      BakedScene baked = bakeScene(scene);
      access::RuntimeWorldAccess boundary(baked);
      std::vector<access::Command> commands = accepted;
      commands.insert(commands.begin() + static_cast<std::ptrdiff_t>(position), command);
      requireAborted(boundary, guids, std::move(commands), position, error);
      CHECK_FALSE(boundary.findEntity(fresh));
    }
  }
}

TEST_CASE("world access transactions: the light limits hold at every step inside a transaction",
          "[world][access][transaction]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  const std::vector<EntityGuid> guids = trackedGuids(scene);
  const EntityGuid scenePoint = scene.entityGuid(kPointNode);

  SECTION("a 65th Point light arising mid-transaction aborts it") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    for (std::uint32_t i = 0; i < access::kMaxPointLights - 1; ++i) addPointLight(boundary, newGuid(100 + i));
    (void)boundary.drainEvents();  // the setup's own events
    std::vector<access::Command> commands = {
        set(scene.entityGuid(kDirectionalNode), typeOf<Light>(), field("world::Light", "intensity"), 2.0f)};
    for (auto& c : pointLightGroup(newGuid(1))) commands.push_back(std::move(c));
    requireAborted(boundary, guids, std::move(commands), 4, access::AccessError::LightLimitExceeded);
    CHECK(pointLights(baked) == access::kMaxPointLights);
  }
  SECTION("a second Directional light arising mid-transaction aborts it") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    const EntityGuid d = newGuid(2);
    requireAborted(boundary, guids,
                   {access::CreateEntity{d}, access::AddComponent{d, typeOf<WorldMatrix>()},
                    access::AddComponent{d, typeOf<Light>()}},
                   2, access::AccessError::LightLimitExceeded);
  }
  SECTION("an earlier command frees the room a later one takes") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    for (std::uint32_t i = 0; i < access::kMaxPointLights - 1; ++i) addPointLight(boundary, newGuid(100 + i));
    (void)boundary.drainEvents();  // the setup's own events
    std::vector<access::Command> commands = {access::DestroyEntity{scenePoint}};
    for (auto& c : pointLightGroup(newGuid(1))) commands.push_back(std::move(c));
    const Outcome outcome = runTransaction(boundary, commands);
    CHECK(outcome.failures.empty());
    CHECK(outcome.events.size() == commands.size());
    CHECK_FALSE(boundary.findEntity(scenePoint));
    CHECK(boundary.findEntity(newGuid(1)));
    CHECK(pointLights(baked) == access::kMaxPointLights);
  }
  SECTION("an earlier command takes the last slot, and a later one is refused") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    for (std::uint32_t i = 0; i < access::kMaxPointLights - 2; ++i) addPointLight(boundary, newGuid(100 + i));
    (void)boundary.drainEvents();  // the setup's own events
    std::vector<access::Command> commands = pointLightGroup(newGuid(1));
    for (auto& c : pointLightGroup(newGuid(2))) commands.push_back(std::move(c));
    requireAborted(boundary, guids, std::move(commands), 8, access::AccessError::LightLimitExceeded);
    CHECK(pointLights(baked) == access::kMaxPointLights - 1);
  }
  SECTION("a Directional light turned Point frees the Directional slot for a later light") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    const EntityGuid d = newGuid(3);
    const Outcome outcome = runTransaction(
        boundary, {set(scene.entityGuid(kDirectionalNode), typeOf<Light>(), field("world::Light", "kind"),
                       access::EnumValue{1}),
                   access::CreateEntity{d}, access::AddComponent{d, typeOf<WorldMatrix>()},
                   access::AddComponent{d, typeOf<Light>()}});
    CHECK(outcome.failures.empty());
    CHECK(outcome.events.size() == 4);
    CHECK(pointLights(baked) == 2);
  }
}

TEST_CASE("world access transactions: ruling Q4's special cases", "[world][access][transaction]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  const std::vector<EntityGuid> guids = trackedGuids(scene);
  const EntityGuid bare = scene.entityGuid(kBareNode);
  const EntityGuid camera = scene.entityGuid(kCameraNode);

  SECTION("(4a) a command after a projected destroy sees the GUID as unknown") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    requireAborted(boundary, guids,
                   {access::DestroyEntity{bare}, set(bare, typeOf<Transform>(),
                                                     field("world::Transform", "localPosition"),
                                                     std::array<float, 3>{1.0f, 1.0f, 1.0f})},
                   1, access::AccessError::UnknownEntity);
    requireAborted(boundary, guids, {access::DestroyEntity{bare}, access::DestroyEntity{bare}}, 1,
                   access::AccessError::UnknownEntity);
    CHECK(boundary.findEntity(bare));
  }
  SECTION("(4b) destroy then re-create of the same GUID is allowed, as one by one") {
    BakedScene bakedA = bakeScene(scene);
    BakedScene bakedB = bakeScene(scene);
    access::RuntimeWorldAccess singly(bakedA);
    access::RuntimeWorldAccess grouped(bakedB);
    const EntityGuid point = scene.entityGuid(kPointNode);
    const std::vector<access::Command> commands = {access::DestroyEntity{point}, access::CreateEntity{point},
                                                   access::AddComponent{point, typeOf<Light>()}};
    const std::vector<access::Event> expected = runSingly(singly, commands);
    const Outcome outcome = runTransaction(grouped, commands);
    CHECK(outcome.failures.empty());
    CHECK(outcome.events == expected);
    CHECK(grouped.listComponents(point).value() == std::vector<TypeId>{typeOf<Light>()});
    CHECK(snapshot(grouped, guids) == snapshot(singly, guids));
  }
  SECTION("(4c) an active-camera refusal anywhere aborts; there is no end-of-transaction check") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    const access::Command edit = set(bare, typeOf<Transform>(), field("world::Transform", "localPosition"),
                                     std::array<float, 3>{1.0f, 1.0f, 1.0f});
    requireAborted(boundary, guids, {access::DestroyEntity{camera}, edit, edit}, 0,
                   access::AccessError::ActiveCameraProtected);
    requireAborted(boundary, guids, {edit, access::RemoveComponent{camera, typeOf<Camera>()}, edit}, 1,
                   access::AccessError::ActiveCameraProtected);
    requireAborted(boundary, guids, {edit, edit, access::RemoveComponent{camera, typeOf<WorldMatrix>()}}, 2,
                   access::AccessError::ActiveCameraProtected);
    // The end state would still have the camera's WorldMatrix; each step is checked.
    requireAborted(boundary, guids,
                   {access::RemoveComponent{camera, typeOf<WorldMatrix>()},
                    access::AddComponent{camera, typeOf<WorldMatrix>()}},
                   0, access::AccessError::ActiveCameraProtected);
    CHECK(boundary.findEntity(camera));
  }
  SECTION("(4d) an empty transaction is a no-op that takes no ticket") {
    BakedScene baked = bakeScene(scene);
    access::RuntimeWorldAccess boundary(baked);
    const Snapshot before = snapshot(boundary, guids);
    const access::TransactionTicket empty = boundary.submitTransaction({});
    CHECK(empty == access::TransactionTicket{access::CommandTicket{}, 0});
    CHECK_FALSE(empty.contains(access::CommandTicket{}));
    const access::ApplyReport report = boundary.applyPending();
    CHECK(report.applied == 0);
    CHECK(report.failures.empty());
    CHECK(boundary.drainEvents().empty());
    CHECK(boundary.drainFailures().empty());
    CHECK(snapshot(boundary, guids) == before);
    CHECK(boundary.submit(access::CreateEntity{newGuid(1)}) == access::CommandTicket{1});
  }
}

TEST_CASE("world access transactions: single commands and transactions apply in submission order",
          "[world][access][transaction]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  BakedScene baked = bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const EntityGuid g = newGuid(1);
  const EntityGuid h = newGuid(2);
  const auto intensity = field("world::Light", "intensity");

  // Tickets: 1, then 2-3, then 4. A transaction depends on the single command
  // before it, and the single command after it on the transaction.
  CHECK(boundary.submit(access::CreateEntity{g}) == access::CommandTicket{1});
  const access::TransactionTicket ticket = boundary.submitTransaction(
      {access::AddComponent{g, typeOf<Light>()}, set(g, typeOf<Light>(), intensity, 4.0f)});
  CHECK(ticket == access::TransactionTicket{access::CommandTicket{2}, 2});
  CHECK(boundary.submit(set(g, typeOf<Light>(), intensity, 5.0f)) == access::CommandTicket{4});
  // An aborted transaction (5-6) does not stop the commands after it (7).
  const access::TransactionTicket aborted =
      boundary.submitTransaction({access::CreateEntity{h}, access::DestroyEntity{newGuid(900)}});
  CHECK(boundary.submit(access::CreateEntity{newGuid(3)}) == access::CommandTicket{7});

  const access::ApplyReport report = boundary.applyPending();
  CHECK(report.applied == 5);
  REQUIRE(report.failures.size() == 1);
  CHECK(report.failures[0] == access::CommandFailure{access::CommandTicket{6}, access::AccessError::UnknownEntity});
  CHECK(aborted.contains(report.failures[0].ticket));
  CHECK_FALSE(ticket.contains(report.failures[0].ticket));
  CHECK(boundary.drainEvents() ==
        std::vector<access::Event>{access::EntityCreated{g}, access::ComponentAdded{g, typeOf<Light>()},
                                   access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::PropertyChanged{{g, typeOf<Light>(), intensity}, 5.0f},
                                   access::EntityCreated{newGuid(3)}});
  CHECK_FALSE(boundary.findEntity(h));
  CHECK(boundary.getProperty({g, typeOf<Light>(), intensity}).value() == access::PropertyValue{5.0f});

  // contains(): exactly the transaction's own tickets.
  CHECK_FALSE(ticket.contains(access::CommandTicket{1}));
  CHECK(ticket.contains(access::CommandTicket{2}));
  CHECK(ticket.contains(access::CommandTicket{3}));
  CHECK_FALSE(ticket.contains(access::CommandTicket{4}));
}

TEST_CASE("world access transactions: two transactions in one apply, the second built on the first",
          "[world][access][transaction]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  BakedScene baked = bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const EntityGuid g = newGuid(1);
  (void)boundary.submitTransaction(pointLightGroup(g));
  (void)boundary.submitTransaction({access::RemoveComponent{g, typeOf<WorldMatrix>()}, access::DestroyEntity{g}});
  const access::ApplyReport report = boundary.applyPending();
  CHECK(report.failures.empty());
  CHECK(report.applied == 7);
  CHECK_FALSE(boundary.findEntity(g));
  CHECK(pointLights(baked) == 1);
}

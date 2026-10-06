#include <atlantis/world/access/runtime_world_access.h>

#include "access/property_access.h"

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/scene_semantic_schema.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Plan 0052 M3 (Spec 0052 R8; rulings Q6, Q8; Corrections J1, J3): what a
// command is refused for -- identity, type and value checks, the two
// light-count crash guards counted over Light + WorldMatrix holders, and the
// active camera's protection. A refused command leaves the world unchanged.

namespace {

constexpr std::string_view kTestTag = "world_access_validation_tests";

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

[[nodiscard]] access::AccessError refusal(access::RuntimeWorldAccess& boundary, access::Command command) {
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

TEST_CASE("world access validation: identity, component and field refusals", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid bare = scene.entityGuid(kBareNode);

  CHECK(refusal(boundary, access::CreateEntity{EntityGuid{}}) == access::AccessError::NilGuid);
  CHECK(refusal(boundary, access::CreateEntity{bare}) == access::AccessError::DuplicateGuid);
  CHECK(refusal(boundary, access::DestroyEntity{newGuid(9)}) == access::AccessError::UnknownEntity);
  CHECK(refusal(boundary, access::AddComponent{bare, atlantis::schema::typeId("world::CameraFog")}) ==
        access::AccessError::UnknownComponentType);
  CHECK(refusal(boundary, access::AddComponent{bare, typeOf<Transform>()}) ==
        access::AccessError::ComponentAlreadyPresent);
  CHECK(refusal(boundary, access::RemoveComponent{bare, typeOf<Light>()}) == access::AccessError::ComponentMissing);
  CHECK(refusal(boundary, set(bare, typeOf<Light>(), field("world::Light", "range"), 1.0f)) ==
        access::AccessError::ComponentMissing);
  CHECK(refusal(boundary, set(bare, typeOf<Transform>(), field("world::Light", "range"), 1.0f)) ==
        access::AccessError::UnknownField);
  CHECK(boundary.drainEvents().empty());
}

TEST_CASE("world access validation: value refusals -- kind, Absent, enum range, non-finite", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid point = scene.entityGuid(kPointNode);
  const EntityGuid renderable = scene.entityGuid(kRenderableNode);
  const Light before = baked.world.get<Light>(*baked.entities.find(point)).value();

  CHECK(refusal(boundary, set(point, typeOf<Light>(), field("world::Light", "intensity"), std::array<float, 3>{})) ==
        access::AccessError::KindMismatch);
  CHECK(refusal(boundary, set(point, typeOf<Light>(), field("world::Light", "intensity"), access::Absent{})) ==
        access::AccessError::KindMismatch);
  CHECK(refusal(boundary, set(point, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{7})) ==
        access::AccessError::EnumValueOutOfRange);
  CHECK(refusal(boundary, set(point, typeOf<Light>(), field("world::Light", "intensity"),
                              std::numeric_limits<float>::quiet_NaN())) == access::AccessError::NonFiniteValue);
  CHECK(refusal(boundary, set(point, typeOf<WorldMatrix>(), field("world::WorldMatrix", "column0"),
                              std::array<float, 4>{0.0f, -std::numeric_limits<float>::infinity(), 0.0f, 0.0f})) ==
        access::AccessError::NonFiniteValue);
  CHECK(refusal(boundary, set(renderable, typeOf<Renderable>(), field("world::Renderable", "meshAsset"),
                              access::Absent{})) == access::AccessError::KindMismatch);
  const Light after = baked.world.get<Light>(*baked.entities.find(point)).value();
  CHECK(std::memcmp(&before, &after, sizeof(Light)) == 0);

  // Absent is a value for the Optional field.
  boundary.submit(set(renderable, typeOf<Renderable>(), field("world::Renderable", "materialAsset"), access::Absent{}));
  CHECK(boundary.applyPending().failures.empty());
  CHECK_FALSE(baked.world.get<Renderable>(*baked.entities.find(renderable)).value().materialAsset.has_value());
}

TEST_CASE("world access validation: a field not flagged Editable is refused", "[world][access]") {
  // Every World field is Editable today (Plans 0048 J3, 0051 J3), so the check
  // is exercised on a resolved field with the flag cleared.
  auto resolved = access::detail::resolveField(typeOf<atlantis::world::Light>(), field("world::Light", "range")).value();
  resolved.flags = atlantis::schema::FieldFlags::Serializable;
  CHECK(access::detail::checkValue(resolved, 1.0f).error() == access::AccessError::FieldNotEditable);
}

TEST_CASE("world access validation: the light limits are the scene schema's and extraction's", "[world][access]") {
  std::uint32_t directional = 0;
  std::uint32_t point = 0;
  for (const auto& rule : atlantis::asset_system::scene::sceneSchema().constraints) {
    if (rule.id == atlantis::asset_system::scene::Constraint::MaxDirectionalLights) directional = rule.limit;
    if (rule.id == atlantis::asset_system::scene::Constraint::MaxPointLights) point = rule.limit;
  }
  CHECK(access::kMaxDirectionalLights == directional);
  CHECK(access::kMaxPointLights == point);
}

TEST_CASE("world access validation: a second Directional light is refused where it would become visible to "
          "extraction (J1)",
          "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid point = scene.entityGuid(kPointNode);

  // Turning the Point light (it has a WorldMatrix) Directional.
  CHECK(refusal(boundary, set(point, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{0})) ==
        access::AccessError::LightLimitExceeded);
  CHECK(baked.world.get<Light>(*baked.entities.find(point)).value().kind == LightKind::Point);

  // A default (Directional) Light is accepted while the entity has no
  // WorldMatrix -- extraction does not see it -- and refused when the
  // WorldMatrix would make it visible.
  const EntityGuid g = newGuid(4);
  boundary.submit(access::CreateEntity{g});
  boundary.submit(access::AddComponent{g, typeOf<Light>()});
  CHECK(boundary.applyPending().failures.empty());
  CHECK(refusal(boundary, access::AddComponent{g, typeOf<WorldMatrix>()}) == access::AccessError::LightLimitExceeded);

  // And the other order: WorldMatrix first, then a default Light.
  const EntityGuid h = newGuid(5);
  boundary.submit(access::CreateEntity{h});
  boundary.submit(access::AddComponent{h, typeOf<WorldMatrix>()});
  CHECK(boundary.applyPending().failures.empty());
  CHECK(refusal(boundary, access::AddComponent{h, typeOf<Light>()}) == access::AccessError::LightLimitExceeded);
  CHECK(boundary.listComponents(h).value() == std::vector<atlantis::schema::TypeId>{typeOf<WorldMatrix>()});
}

TEST_CASE("world access validation: a 65th Point light is refused (J1)", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  // The scene has one Point light; 63 more reach the limit of 64.
  for (std::uint32_t i = 0; i < access::kMaxPointLights - 1; ++i) addPointLight(boundary, newGuid(100 + i));

  const EntityGuid extra = newGuid(1000);
  boundary.submit(access::CreateEntity{extra});
  boundary.submit(access::AddComponent{extra, typeOf<Light>()});
  boundary.submit(set(extra, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{1}));
  CHECK(boundary.applyPending().failures.empty());  // no WorldMatrix yet: not counted
  CHECK(refusal(boundary, access::AddComponent{extra, typeOf<WorldMatrix>()}) ==
        access::AccessError::LightLimitExceeded);

  // The scene's Directional light (with its WorldMatrix) turned Point.
  CHECK(refusal(boundary, set(scene.entityGuid(kDirectionalNode), typeOf<Light>(), field("world::Light", "kind"),
                              access::EnumValue{1})) == access::AccessError::LightLimitExceeded);

  std::uint32_t points = 0;
  baked.world.query<const Light, const WorldMatrix>([&](ecs::EntityId, const Light& light, const WorldMatrix&) {
    if (light.kind == LightKind::Point) ++points;
  });
  CHECK(points == access::kMaxPointLights);
}

TEST_CASE("world access validation: the active camera and its Camera/WorldMatrix are protected (J3)",
          "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid camera = scene.entityGuid(kCameraNode);

  CHECK(refusal(boundary, access::DestroyEntity{camera}) == access::AccessError::ActiveCameraProtected);
  CHECK(refusal(boundary, access::RemoveComponent{camera, typeOf<Camera>()}) ==
        access::AccessError::ActiveCameraProtected);
  CHECK(refusal(boundary, access::RemoveComponent{camera, typeOf<WorldMatrix>()}) ==
        access::AccessError::ActiveCameraProtected);
  CHECK(boundary.findEntity(camera));

  // Its other data stays editable, and other entities' cameras are not protected.
  boundary.submit(access::RemoveComponent{camera, typeOf<Transform>()});
  boundary.submit(set(camera, typeOf<Camera>(), field("world::CameraBloom", "strength"), 0.5f));
  CHECK(boundary.applyPending().failures.empty());
}

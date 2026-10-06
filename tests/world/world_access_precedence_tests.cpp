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

// Plan 0053 M1 (Spec 0053 R6, Correction 2026-10-07; Plan 0053 J6): the order
// in which Spec 0052's checks refuse a command that has several faults, pinned
// against the boundary as merged in PR #216, before Plan 0053 M2 moves the
// checks into one state-view routine. Every case here must keep passing,
// unmodified, after that refactor.

namespace {

constexpr std::string_view kTestTag = "world_access_precedence_tests";

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

// Each case below carries two or more faults; the refusal reported is the
// first in today's order (Plan 0053, pre-drafting reading item 2):
//   CreateEntity    NilGuid -> DuplicateGuid
//   DestroyEntity   UnknownEntity -> ActiveCameraProtected
//   AddComponent    UnknownEntity -> UnknownComponentType -> ComponentAlreadyPresent -> LightLimitExceeded
//   RemoveComponent UnknownEntity -> UnknownComponentType -> ComponentMissing -> ActiveCameraProtected
//   SetProperty     UnknownEntity -> UnknownComponentType / UnknownField -> ComponentMissing
//                   -> FieldNotEditable -> KindMismatch -> EnumValueOutOfRange -> NonFiniteValue
//                   -> LightLimitExceeded

TEST_CASE("world access precedence: AddComponent reports its first refusal", "[world][access][precedence]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const auto unknownType = atlantis::schema::typeId("world::CameraFog");

  // Unknown entity and unknown component type.
  CHECK(refusal(boundary, access::AddComponent{newGuid(9), unknownType}) == access::AccessError::UnknownEntity);
  // Unknown entity and a component the (absent) entity could not take.
  CHECK(refusal(boundary, access::AddComponent{newGuid(9), typeOf<Light>()}) == access::AccessError::UnknownEntity);
  // A Light already present on the Point light, whose default (Directional)
  // kind together with its WorldMatrix would also exceed the Directional limit.
  CHECK(refusal(boundary, access::AddComponent{scene.entityGuid(kPointNode), typeOf<Light>()}) ==
        access::AccessError::ComponentAlreadyPresent);
  // A WorldMatrix already present on the Directional light.
  CHECK(refusal(boundary, access::AddComponent{scene.entityGuid(kDirectionalNode), typeOf<WorldMatrix>()}) ==
        access::AccessError::ComponentAlreadyPresent);
  CHECK(boundary.drainEvents().empty());
}

TEST_CASE("world access precedence: RemoveComponent and DestroyEntity report their first refusal",
          "[world][access][precedence]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid camera = scene.entityGuid(kCameraNode);
  const auto unknownType = atlantis::schema::typeId("world::CameraFog");

  CHECK(refusal(boundary, access::RemoveComponent{newGuid(9), unknownType}) == access::AccessError::UnknownEntity);
  // The active camera, with a TypeId that is not a World component.
  CHECK(refusal(boundary, access::RemoveComponent{camera, unknownType}) == access::AccessError::UnknownComponentType);
  // The active camera, with a component it does not hold: not protected, missing.
  CHECK(refusal(boundary, access::RemoveComponent{camera, typeOf<Light>()}) == access::AccessError::ComponentMissing);
  CHECK(refusal(boundary, access::RemoveComponent{camera, typeOf<Camera>()}) ==
        access::AccessError::ActiveCameraProtected);
  CHECK(refusal(boundary, access::DestroyEntity{newGuid(9)}) == access::AccessError::UnknownEntity);
  CHECK(refusal(boundary, access::DestroyEntity{camera}) == access::AccessError::ActiveCameraProtected);
  // Nil is checked before uniqueness.
  CHECK(refusal(boundary, access::CreateEntity{EntityGuid{}}) == access::AccessError::NilGuid);
  CHECK(boundary.findEntity(camera));
  CHECK(boundary.drainEvents().empty());
}

TEST_CASE("world access precedence: SetProperty reports its first refusal", "[world][access][precedence]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid bare = scene.entityGuid(kBareNode);
  const EntityGuid point = scene.entityGuid(kPointNode);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const auto lightKind = field("world::Light", "kind");
  const auto intensity = field("world::Light", "intensity");

  // Unknown entity, unknown field, non-finite value.
  CHECK(refusal(boundary, set(newGuid(9), typeOf<Light>(), field("world::Transform", "localPosition"), nan)) ==
        access::AccessError::UnknownEntity);
  // A TypeId that is not a World component, with a non-finite value.
  CHECK(refusal(boundary, set(bare, atlantis::schema::typeId("world::CameraFog"), field("world::CameraFog", "density"),
                              nan)) == access::AccessError::UnknownComponentType);
  // A field not under the component, on an entity lacking the component.
  CHECK(refusal(boundary, set(bare, typeOf<Light>(), field("world::Transform", "localPosition"), nan)) ==
        access::AccessError::UnknownField);
  // A missing component, a wrong kind and a non-finite value.
  CHECK(refusal(boundary, set(bare, typeOf<Light>(), intensity, std::array<float, 3>{nan, nan, nan})) ==
        access::AccessError::ComponentMissing);
  CHECK(refusal(boundary, set(bare, typeOf<Light>(), intensity, nan)) == access::AccessError::ComponentMissing);
  // A wrong kind holding a non-finite value.
  CHECK(refusal(boundary, set(point, typeOf<Light>(), intensity, std::array<float, 3>{nan, 0.0f, 0.0f})) ==
        access::AccessError::KindMismatch);
  // Light.kind on a Point light with a WorldMatrix: a wrong kind and an
  // out-of-range enum are reported before the light limit is consulted.
  CHECK(refusal(boundary, set(point, typeOf<Light>(), lightKind, 0.0f)) == access::AccessError::KindMismatch);
  CHECK(refusal(boundary, set(point, typeOf<Light>(), lightKind, access::EnumValue{7})) ==
        access::AccessError::EnumValueOutOfRange);
  CHECK(refusal(boundary, set(point, typeOf<Light>(), lightKind, access::EnumValue{0})) ==
        access::AccessError::LightLimitExceeded);
  CHECK(boundary.drainEvents().empty());
}

TEST_CASE("world access precedence: the value checks run Editable, kind, enum range, finiteness",
          "[world][access][precedence]") {
  // Every World field is Editable today, so Editable's place is pinned on a
  // resolved field with the flag cleared (as Spec 0052's validation tests do).
  using atlantis::world::Light;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  auto range = access::detail::resolveField(typeOf<Light>(), field("world::Light", "range")).value();
  range.flags = atlantis::schema::FieldFlags::Serializable;
  CHECK(access::detail::checkValue(range, std::array<float, 3>{nan, nan, nan}).error() ==
        access::AccessError::FieldNotEditable);
  CHECK(access::detail::checkValue(range, nan).error() == access::AccessError::FieldNotEditable);
  auto kind = access::detail::resolveField(typeOf<Light>(), field("world::Light", "kind")).value();
  CHECK(access::detail::checkValue(kind, nan).error() == access::AccessError::KindMismatch);
  kind.flags = atlantis::schema::FieldFlags::Serializable;
  CHECK(access::detail::checkValue(kind, access::EnumValue{7}).error() == access::AccessError::FieldNotEditable);
}

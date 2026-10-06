#include <atlantis/world/access/runtime_world_access.h>

#include "access/property_access.h"

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// Plan 0052 M2 (Spec 0052 R1-R3, R6; rulings Q1, Q3, Q7): the boundary's
// queries over a real baked scene -- FindEntity, ListComponents and
// GetProperty, by value, against the ECS's own view -- and the public
// headers' layout independence.

namespace {

constexpr std::string_view kTestTag = "world_access_query_tests";

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

using atlantis::schema::FieldDescriptor;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeKind;

[[nodiscard]] const TypeDescriptor& requireType(atlantis::schema::TypeId id) {
  for (const TypeDescriptor& type : atlantis::world::worldSchema()) {
    if (type.id == id) return type;
  }
  FAIL("type not in worldSchema()");
  return atlantis::world::worldSchema()[0];
}

void collectLeafIds(const TypeDescriptor& type, std::vector<atlantis::schema::FieldId>& out) {
  for (const FieldDescriptor& field : type.fields) {
    if (field.kind == TypeKind::Struct) {
      collectLeafIds(requireType(field.type), out);
    } else {
      out.push_back(field.id);
    }
  }
}

// Every leaf of a present component reads, through the boundary, exactly
// what the M1-verified accessor reads from the ECS's own copy of the
// component -- so GetProperty adds lookup, not a second interpretation.
template <typename T>
void expectEveryLeafMatches(access::RuntimeWorldAccess& boundary, const ecs::World& world, ecs::EntityId id,
                            const EntityGuid& guid) {
  const auto component = world.get<T>(id);
  if (component.isErr()) return;
  const T copy = component.value();
  const atlantis::schema::TypeId type = ecs::componentTypeId<T>();
  std::vector<atlantis::schema::FieldId> leaves;
  collectLeafIds(requireType(type), leaves);
  for (const auto field : leaves) {
    const auto value = boundary.getProperty({guid, type, field});
    REQUIRE(value.isOk());
    CHECK(value.value() == atlantis::world::access::detail::readField(
                               reinterpret_cast<const std::byte*>(&copy), type, field,
                               atlantis::world::access::detail::resolveField(type, field).value()));
  }
}

}  // namespace

TEST_CASE("world access query: FindEntity finds every baked node and nothing else", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  const access::RuntimeWorldAccess boundary(baked);
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) CHECK(boundary.findEntity(scene.entityGuid(i)));
  CHECK_FALSE(boundary.findEntity(atlantis::asset_system::parseEntityGuid("52052052-ffff-4052-8052-00000000ffff").value()));
  CHECK_FALSE(boundary.findEntity(EntityGuid{}));
}

TEST_CASE("world access query: ListComponents returns each entity's actual component set, sorted", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  const access::RuntimeWorldAccess boundary(baked);
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    INFO("node " << i);
    const auto listed = boundary.listComponents(scene.entityGuid(i));
    REQUIRE(listed.isOk());
    const ecs::EntityId id = *baked.entities.find(scene.entityGuid(i));
    std::vector<atlantis::schema::TypeId> expected;
    std::apply(
        [&](auto... tag) {
          ((baked.world.has<decltype(tag)>(id).value() ? expected.push_back(ecs::componentTypeId<decltype(tag)>())
                                                        : void()),
           ...);
        },
        ecs::WorldComponentTypes{});
    std::sort(expected.begin(), expected.end());
    CHECK(listed.value() == expected);
    CHECK(std::is_sorted(listed.value().begin(), listed.value().end()));
  }
  CHECK(boundary.listComponents(EntityGuid{}).error() == access::AccessError::UnknownEntity);
}

TEST_CASE("world access query: GetProperty returns, by value, the field the ECS holds", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;

  const EntityGuid renderableNode = scene.entityGuid(0);
  const EntityGuid pointLight = scene.entityGuid(2);
  const EntityGuid camera = scene.entityGuid(3);
  const ecs::EntityId renderableId = *baked.entities.find(renderableNode);
  const ecs::EntityId pointId = *baked.entities.find(pointLight);
  const ecs::EntityId cameraId = *baked.entities.find(camera);

  // Transform.localScale (Vec3), WorldMatrix.column3 (Vec4), Light.kind
  // (enum), Light.range (float), Camera.fog.density (nested), and the
  // Optional materialAsset, against the ECS's own components.
  const Transform transform = baked.world.get<Transform>(renderableId).value();
  CHECK(boundary.getProperty({renderableNode, ecs::componentTypeId<Transform>(),
                              atlantis::schema::fieldId("world::Transform", "localScale")})
            .value() == access::PropertyValue{std::array<float, 3>{transform.localScale.x, transform.localScale.y,
                                                                   transform.localScale.z}});
  const WorldMatrix matrix = baked.world.get<WorldMatrix>(pointId).value();
  CHECK(boundary.getProperty({pointLight, ecs::componentTypeId<WorldMatrix>(),
                              atlantis::schema::fieldId("world::WorldMatrix", "column3")})
            .value() == access::PropertyValue{matrix.column3});
  CHECK(boundary.getProperty({pointLight, ecs::componentTypeId<Light>(), atlantis::schema::fieldId("world::Light", "kind")})
            .value() == access::PropertyValue{access::EnumValue{1}});
  CHECK(boundary.getProperty({pointLight, ecs::componentTypeId<Light>(), atlantis::schema::fieldId("world::Light", "range")})
            .value() == access::PropertyValue{2.5f});
  const Camera cameraComponent = baked.world.get<Camera>(cameraId).value();
  CHECK(boundary.getProperty({camera, ecs::componentTypeId<Camera>(), atlantis::schema::fieldId("world::CameraFog", "density")})
            .value() == access::PropertyValue{cameraComponent.fog.density});
  const Renderable renderable = baked.world.get<Renderable>(renderableId).value();
  REQUIRE(renderable.materialAsset.has_value());
  CHECK(boundary.getProperty({renderableNode, ecs::componentTypeId<Renderable>(),
                              atlantis::schema::fieldId("world::Renderable", "materialAsset")})
            .value() == access::PropertyValue{std::uint64_t{*renderable.materialAsset}});

  // Every leaf of every component of every node reads successfully.
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    const ecs::EntityId id = *baked.entities.find(scene.entityGuid(i));
    expectEveryLeafMatches<Transform>(boundary, baked.world, id, scene.entityGuid(i));
    expectEveryLeafMatches<Camera>(boundary, baked.world, id, scene.entityGuid(i));
    expectEveryLeafMatches<Light>(boundary, baked.world, id, scene.entityGuid(i));
    expectEveryLeafMatches<Renderable>(boundary, baked.world, id, scene.entityGuid(i));
    expectEveryLeafMatches<WorldMatrix>(boundary, baked.world, id, scene.entityGuid(i));
  }

  // Errors: unknown entity, a missing component, an unknown type and field.
  CHECK(boundary.getProperty({EntityGuid{}, ecs::componentTypeId<Light>(), atlantis::schema::fieldId("world::Light", "range")})
            .error() == access::AccessError::UnknownEntity);
  CHECK(boundary.getProperty({camera, ecs::componentTypeId<Light>(), atlantis::schema::fieldId("world::Light", "range")})
            .error() == access::AccessError::ComponentMissing);
  CHECK(boundary.getProperty({camera, atlantis::schema::typeId("world::CameraFog"),
                              atlantis::schema::fieldId("world::CameraFog", "density")})
            .error() == access::AccessError::UnknownComponentType);
  CHECK(boundary.getProperty({camera, ecs::componentTypeId<Camera>(), atlantis::schema::fieldId("world::Light", "range")})
            .error() == access::AccessError::UnknownField);
}

TEST_CASE("world access query: the public headers name no ECS handle, archetype or component C++ type",
          "[world][access][module_boundary]") {
  const fs::path root = fs::path(ATLANTIS_WORLD_SOURCE_DIR) / "include" / "atlantis" / "world" / "access";
  REQUIRE(fs::exists(root));
  std::size_t scanned = 0;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.path().extension() != ".h") continue;
    ++scanned;
    std::ifstream in(entry.path());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    INFO(entry.path().string());
    for (const char* forbidden : {"ecs::", "EntityId", "Archetype", "Chunk", "world::Transform", "world::Camera",
                                  "world::Light", "world::Renderable", "world::WorldMatrix", "#include <atlantis/world/ecs"}) {
      INFO(forbidden);
      CHECK(text.find(forbidden) == std::string::npos);
    }
  }
  CHECK(scanned == 2);
}

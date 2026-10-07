#include <atlantis/cli/command.h>
#include <atlantis/cli/script_runner.h>
#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/connection/text.h>
#include <atlantis/asset_system/asset_catalog.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/access/runtime_world_access.h>
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
#include <iterator>
#include <memory>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Plan 0054 M4 (Spec 0054 R6, R7; rulings Q2, Q6): the six CLI commands over an
// InProcess connection on a baked fixture scene, against their expected
// output; entity set's outcome after the frame that applied it; and the
// get -> set round trip of every leaf.

namespace {

constexpr std::string_view kTestTag = "cli_command_tests";

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

[[maybe_unused]] [[nodiscard]] access::SetProperty set(const EntityGuid& entity, atlantis::schema::TypeId component,
                                      atlantis::schema::FieldId fieldId, access::PropertyValue value) {
  return access::SetProperty{{entity, component, fieldId}, std::move(value)};
}

// A Point light with a WorldMatrix -- one extraction counts -- built through
// the boundary in the order Correction J1 allows.
[[maybe_unused]] void addPointLight(access::RuntimeWorldAccess& boundary, const EntityGuid& g) {
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

using atlantis::cli::Commands;
using atlantis::cli::Outcome;
using atlantis::connection::InProcessEndpoint;
using atlantis::schema::TypeId;
namespace text = atlantis::connection::text;

// A baked fixture scene with the boundary, an endpoint and one connection.
struct Session {
  ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary{baked};
  InProcessEndpoint endpoint{boundary};
  std::unique_ptr<atlantis::connection::RuntimeConnection> connection = endpoint.open();
  std::ostringstream out;
  Commands commands{*connection, out};

  // Runs one line and returns what it printed.
  std::string run(std::string_view line, Outcome expected) {
    out.str({});
    CHECK(commands.run(line) == expected);
    return out.str();
  }
  // Applies pending commands (the owner's frame) and reports the set.
  std::string applyAndReport(Outcome expected) {
    (void)boundary.applyPending();
    out.str({});
    CHECK(commands.reportPending() == expected);
    return out.str();
  }
  [[nodiscard]] std::string guid(std::size_t node) const { return text::formatEntity(scene.entityGuid(node)); }
};

}  // namespace

TEST_CASE("cli: schema list and schema inspect", "[cli]") {
  Session s;
  CHECK(s.run("schema list", Outcome::Done) ==
        "Transform struct\nCameraFog struct\nCameraBloom struct\nCamera struct\nLight struct\nLightKind enum\n"
        "Renderable struct\nWorldMatrix struct\n");
  const std::string light =
      "Light struct (world::Light) v1\n"
      "  kind enum LightKind serializable,editable\n"
      "  color vec3 serializable,editable\n"
      "  intensity float32 serializable,editable\n"
      "  range float32 serializable,editable\n";
  CHECK(s.run("schema inspect Light", Outcome::Done) == light);
  CHECK(s.run("schema inspect world::Light", Outcome::Done) == light);
  CHECK(s.run("schema inspect LightKind", Outcome::Done) ==
        "LightKind enum (world::LightKind) v1\n  Directional = 0\n  Point = 1\n");
  CHECK(s.run("schema inspect Renderable", Outcome::Done) ==
        "Renderable struct (world::Renderable) v1\n"
        "  meshAsset uint64 serializable,editable,assetref\n"
        "  materialAsset uint64 serializable,editable,assetref,optional\n");
  CHECK(s.run("schema inspect Nope", Outcome::Error) == "error: UnknownType 'Nope'\n");
  CHECK(s.run("schema inspect", Outcome::Error) == "error: usage: schema inspect <Type>\n");
}

TEST_CASE("cli: world entities lists every addressable entity in GUID order", "[cli]") {
  Session s;
  // Components in listComponents() order (TypeId value).
  CHECK(s.run("world entities", Outcome::Done) ==
        "52052052-0001-4052-8052-000000000001 WorldMatrix,Transform,Renderable\n"
        "52052052-0002-4052-8052-000000000002 WorldMatrix,Transform,Light\n"
        "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n"
        "52052052-0004-4052-8052-000000000004 WorldMatrix,Transform,Camera\n"
        "52052052-0005-4052-8052-000000000005 WorldMatrix,Transform\n");
}

TEST_CASE("cli: entity inspect prints every leaf of every component", "[cli]") {
  Session s;
  // The point light (node 3) is parented to node 1: its WorldMatrix is the
  // bake's product of both transforms.
  CHECK(s.run("entity inspect " + s.guid(kPointNode), Outcome::Done) ==
        "52052052-0003-4052-8052-000000000003\n"
        "  WorldMatrix.column0 = 0.942154706 0.294043839 -0.160881355 0\n"
        "  WorldMatrix.column1 = -0.541363001 1.9011277 0.304368347 0\n"
        "  WorldMatrix.column2 = 0.197676808 -0.0998334214 0.975170374 0\n"
        "  WorldMatrix.column3 = 1.69015336 2.75565672 3.45019054 1\n"
        "  Transform.localPosition = 0.800000012 0.300000012 0.5\n"
        "  Transform.localEulerAnglesRadians = 0 0 0\n"
        "  Transform.localScale = 1 1 1\n"
        "  Light.kind = Point\n"
        "  Light.color = 1 0.600000024 0.300000012\n"
        "  Light.intensity = 3\n"
        "  Light.range = 2.5\n");
  CHECK(s.run("entity inspect 52052052-0000-4052-8052-000000000099", Outcome::Refused) == "refused UnknownEntity\n");
  CHECK(s.run("entity inspect nope", Outcome::Error) == "error: MalformedGuid 'nope'\n");
}

TEST_CASE("cli: entity get prints one value in its canonical text", "[cli]") {
  Session s;
  const std::string point = s.guid(kPointNode);
  CHECK(s.run("entity get " + point + " Light.intensity", Outcome::Done) == "3\n");
  CHECK(s.run("entity get " + point + " world::Light.kind", Outcome::Done) == "Point\n");
  CHECK(s.run("entity get " + point + " Light.color", Outcome::Done) == "1 0.600000024 0.300000012\n");
  CHECK(s.run("entity get " + s.guid(kCameraNode) + " Camera.fog.density", Outcome::Done) ==
        "0\n");
  CHECK(s.run("entity get " + s.guid(kBareNode) + " Light.intensity", Outcome::Refused) ==
        "refused ComponentMissing\n");
  CHECK(s.run("entity get " + point + " Light.nope", Outcome::Error) == "error: UnknownField 'Light.nope'\n");
  CHECK(s.run("entity get " + point, Outcome::Error) == "error: usage: entity get <guid> <Type>.<field>\n");
}

TEST_CASE("cli: entity set reports its outcome after the frame that applied it", "[cli]") {
  Session s;
  const std::string point = s.guid(kPointNode);
  CHECK(s.run("entity set " + point + " Light.intensity 6", Outcome::Submitted).empty());
  CHECK(s.commands.hasPending());
  // While the set is pending, the next command waits for its outcome.
  CHECK(s.run("entity get " + point + " Light.intensity", Outcome::Error) ==
        "error: the previous `entity set` has no outcome yet\n");
  CHECK(s.applyAndReport(Outcome::Done) == "ok " + point + " Light.intensity = 6\n");
  CHECK(s.run("entity get " + point + " Light.intensity", Outcome::Done) == "6\n");

  // Refused by the boundary: a second Directional light; a non-finite value.
  CHECK(s.run("entity set " + point + " Light.kind Directional", Outcome::Submitted).empty());
  CHECK(s.applyAndReport(Outcome::Refused) == "refused LightLimitExceeded\n");
  CHECK(s.run("entity set " + point + " Light.intensity nan", Outcome::Submitted).empty());
  CHECK(s.applyAndReport(Outcome::Refused) == "refused NonFiniteValue\n");
  // Text errors: nothing is submitted.
  CHECK(s.run("entity set " + point + " Light.kind point", Outcome::Error) == "error: UnknownEnumConstant 'point'\n");
  CHECK(s.run("entity set " + point + " Light.color 1 2", Outcome::Error) == "error: WrongValueCount '1'\n");
  CHECK(s.run("entity set " + point + " Light.intensity", Outcome::Error) ==
        "error: usage: entity set <guid> <Type>.<field> <value...>\n");
  CHECK_FALSE(s.commands.hasPending());
  CHECK(s.run("entity frobnicate", Outcome::Error) == "error: unknown command 'entity frobnicate'\n");
  CHECK(s.run("   ", Outcome::Error) == "error: empty command\n");
}

TEST_CASE("cli: what entity get prints, entity set takes back unchanged (round trip)", "[cli]") {
  Session s;
  const auto snapshot = [&] {
    std::vector<access::PropertyValue> values;
    for (const EntityGuid& entity : s.connection->listEntities()) {
      const std::vector<TypeId> components = s.connection->listComponents(entity).value();
      for (const TypeId component : components) {
        for (const auto& leaf : text::leavesOf(s.connection->schema(), component)) {
          values.push_back(s.connection->getProperty({entity, component, leaf.leaf->id}).value());
        }
      }
    }
    return values;
  };
  const auto before = snapshot();
  std::size_t roundTrips = 0;
  for (const EntityGuid& entity : s.connection->listEntities()) {
    const std::string guid = text::formatEntity(entity);
    const std::vector<TypeId> components = s.connection->listComponents(entity).value();
    for (const TypeId component : components) {
      for (const auto& leaf : text::leavesOf(s.connection->schema(), component)) {
        std::string printed = s.run("entity get " + guid + " " + leaf.path, Outcome::Done);
        printed.pop_back();  // '\n'
        INFO(guid << " " << leaf.path << " = " << printed);
        CHECK(s.run("entity set " + guid + " " + leaf.path + " " + printed, Outcome::Submitted).empty());
        CHECK(s.applyAndReport(Outcome::Done) == "ok " + guid + " " + leaf.path + " = " + printed + "\n");
        ++roundTrips;
      }
    }
  }
  CHECK(roundTrips > 24);
  CHECK(snapshot() == before);
}

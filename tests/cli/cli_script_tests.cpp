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

// Plan 0054 M4 (Spec 0054 R9, R11; ruling Q3; J9): the script runner, one
// command per frame -- echo, comments, blank lines, per-line errors, the done
// line -- and scripts/north_star.txt, the acceptance script, on the default
// scene decoded from the build catalog.

namespace {

constexpr std::string_view kTestTag = "cli_script_tests";

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

using atlantis::cli::ScriptRunner;
using atlantis::connection::InProcessEndpoint;
namespace text = atlantis::connection::text;

// Drives a script the way atlantis_runtime does: after each "frame" (here,
// the owner's applyPending()), one step().
[[nodiscard]] std::string runScript(access::RuntimeWorldAccess& boundary, const std::string& script) {
  InProcessEndpoint endpoint(boundary);
  const auto connection = endpoint.open();
  std::ostringstream out;
  {
    ScriptRunner runner(*connection, atlantis::cli::splitLines(script), out);
    for (int frame = 0; frame < 100 && !runner.finished(); ++frame) {
      (void)boundary.applyPending();
      runner.step();
    }
    CHECK(runner.finished());
  }
  return out.str();
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("cli script: lines split on LF and CRLF", "[cli][script]") {
  CHECK(atlantis::cli::splitLines("a\r\nb\n\nc") == std::vector<std::string>{"a", "b", "", "c"});
  CHECK(atlantis::cli::splitLines("") == std::vector<std::string>{""});
}

TEST_CASE("cli script: one command per frame; comments and blank lines skipped; errors and refusals go on",
          "[cli][script]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const std::string point = text::formatEntity(scene.entityGuid(kPointNode));
  const std::string script = "# a comment\n"
                             "\n"
                             "entity get " + point + " Light.range\n"
                             "  # an indented comment\n"
                             "entity set " + point + " Light.range 4.5\n"
                             "entity get " + point + " Light.range\n"
                             "entity bogus\n"
                             "entity set " + point + " Light.kind Directional\n";
  CHECK(runScript(boundary, script) ==
        "> entity get " + point + " Light.range\n"
        "2.5\n"
        "> entity set " + point + " Light.range 4.5\n"
        "ok " + point + " Light.range = 4.5\n"
        "> entity get " + point + " Light.range\n"
        "4.5\n"
        "> entity bogus\n"
        "error: unknown command 'entity bogus'\n"
        "> entity set " + point + " Light.kind Directional\n"
        "refused LightLimitExceeded\n"
        "# exec done: 5 commands, 1 errors, 1 refused\n");
}

TEST_CASE("cli script: north_star.txt on the default scene (Spec 0054 R11)", "[cli][script]") {
  auto catalog = atlantis::asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());
  const auto guid = atlantis::asset_system::parseAssetGuid(ATLANTIS_DEFAULT_SCENE_GUID);
  REQUIRE(guid.isOk());
  const auto* record = catalog.value().find(guid.value());
  REQUIRE(record != nullptr);
  auto scene = atlantis::asset_system::decodeScene(record->artifact, record->metadata);
  REQUIRE(scene.isOk());
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene.value());
  access::RuntimeWorldAccess boundary(baked);

  const std::string light = "0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea";
  CHECK(runScript(boundary, readFile(std::filesystem::path(ATLANTIS_CLI_SCRIPTS_DIR) / "north_star.txt")) ==
        "> entity get " + light + " Light.intensity\n"
        "3\n"
        "> entity set " + light + " Light.intensity 6\n"
        "ok " + light + " Light.intensity = 6\n"
        "> entity get " + light + " Light.intensity\n"
        "6\n"
        "# exec done: 3 commands, 0 errors, 0 refused\n");
}

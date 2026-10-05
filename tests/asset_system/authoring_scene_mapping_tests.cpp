#include <atlantis/asset_system/authoring_scene_mapping.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_types.h>

// Plan 0049 M2 / P4 (Spec 0049 R2, R5, ruling Q5): ParsedSceneSource <->
// AuthoringScene, exact in both directions; node_ids are syntax.

namespace {

namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;

[[nodiscard]] as::EntityGuid entity(std::uint8_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xe0};
  bytes[15] = std::byte{n};
  return as::entityGuidFromBytes(bytes).value();
}

[[nodiscard]] as::AssetGuid asset(std::uint8_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xa0};
  bytes[15] = std::byte{n};
  return as::assetGuidFromBytes(bytes).value();
}

[[nodiscard]] as::ParsedSceneNode parsedNode(std::uint32_t nodeId, std::uint8_t guid) {
  as::ParsedSceneNode node;
  node.nodeId = nodeId;
  node.entityGuid = entity(guid);
  return node;
}

[[nodiscard]] std::string readText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// Plan 0049 P10: the committed scene sources, enumerated at run time.
[[nodiscard]] std::vector<std::filesystem::path> corpus() {
  const std::filesystem::path assets{ATLANTIS_ASSETS_DIR};
  std::vector<std::filesystem::path> files;
  for (const auto& dir : {assets / "scenes", assets / "_test_fixtures"}) {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().string().ends_with(".scene.txt")) files.push_back(entry.path());
    }
  }
  files.push_back(assets / "bistro" / "bistro_overlay.scene.txt");
  return files;
}

}  // namespace

TEST_CASE("authoring mapping: components convert field by field", "[asset_system][scene][mapping]") {
  as::ParsedSceneSource source;
  as::ParsedSceneNode cameraNode = parsedNode(7, 1);
  cameraNode.transform = {1, 2, 3, 0.1f, 0.2f, 0.3f, 4, 5, 6};
  as::DecodedCamera camera;
  camera.fovYRadians = 1.0f;
  camera.nearZ = 0.5f;
  camera.farZ = 50.0f;
  camera.exposureCompensationEv = -2.0f;
  camera.fog = {0.1f, 0.2f, 0.3f, 0.04f, -1.0f, 0.5f, 0.75f};
  camera.bloom = {0.6f, 2.0f};
  cameraNode.camera = camera;
  as::ParsedSceneNode meshNode = parsedNode(9, 2);
  meshNode.parentNodeId = 7;
  meshNode.meshAsset = asset(1);
  meshNode.materialAsset = asset(2);
  as::ParsedSceneNode lightNode = parsedNode(3, 3);
  lightNode.light = as::DecodedLight{as::DecodedLightKind::Point, 0.25f, 0.5f, 0.75f, 3.0f, 2.5f};
  source.nodes = {cameraNode, meshNode, lightNode};
  source.activeCameraNodeId = 7;

  const auto mapped = scene::toAuthoringScene(source);
  REQUIRE(mapped.isOk());
  const scene::AuthoringScene& s = mapped.value();
  REQUIRE(s.nodes.size() == 3);
  CHECK(s.activeCamera == entity(1));

  const scene::AuthoringNode& c = s.nodes[0];
  CHECK(c.guid == entity(1));
  CHECK_FALSE(c.parent.has_value());
  CHECK(c.transform.localPosition == std::array<float, 3>{1, 2, 3});
  CHECK(c.transform.localEulerAnglesRadians == std::array<float, 3>{0.1f, 0.2f, 0.3f});
  CHECK(c.transform.localScale == std::array<float, 3>{4, 5, 6});
  REQUIRE(c.camera.has_value());
  CHECK(c.camera->fovYRadians == 1.0f);
  CHECK(c.camera->nearZ == 0.5f);
  CHECK(c.camera->farZ == 50.0f);
  CHECK(c.camera->exposureCompensationEv == -2.0f);
  CHECK(c.camera->fog.color == std::array<float, 3>{0.1f, 0.2f, 0.3f});
  CHECK(c.camera->fog.density == 0.04f);
  CHECK(c.camera->fog.height == -1.0f);
  CHECK(c.camera->fog.heightFalloff == 0.5f);
  CHECK(c.camera->fog.maxOpacity == 0.75f);
  CHECK(c.camera->bloom.strength == 0.6f);
  CHECK(c.camera->bloom.threshold == 2.0f);
  CHECK_FALSE(c.renderable.has_value());
  CHECK_FALSE(c.light.has_value());

  const scene::AuthoringNode& m = s.nodes[1];
  CHECK(m.parent == entity(1));
  REQUIRE(m.renderable.has_value());
  CHECK(m.renderable->meshAsset == asset(1));
  CHECK(m.renderable->materialAsset == asset(2));

  const scene::AuthoringNode& l = s.nodes[2];
  REQUIRE(l.light.has_value());
  CHECK(l.light->kind == scene::LightKind::Point);
  CHECK(l.light->color == std::array<float, 3>{0.25f, 0.5f, 0.75f});
  CHECK(l.light->intensity == 3.0f);
  CHECK(l.light->range == 2.5f);

  // And back: node_ids renumbered by order, content equal.
  const auto back = scene::toParsedSceneSource(s);
  REQUIRE(back.isOk());
  CHECK(back.value().nodes[0].nodeId == 1);
  CHECK(back.value().nodes[1].nodeId == 2);
  CHECK(back.value().nodes[1].parentNodeId == 1u);
  CHECK(back.value().activeCameraNodeId == 1u);
  const auto again = scene::toAuthoringScene(back.value());
  REQUIRE(again.isOk());
  CHECK(again.value() == s);
}

TEST_CASE("authoring mapping: semantic defaults equal the Decoded* defaults", "[asset_system][scene][mapping]") {
  as::ParsedSceneSource source;
  as::ParsedSceneNode node = parsedNode(1, 1);
  node.camera = as::DecodedCamera{};
  as::ParsedSceneNode lightNode = parsedNode(2, 2);
  lightNode.light = as::DecodedLight{};
  source.nodes = {node, lightNode};
  const auto mapped = scene::toAuthoringScene(source);
  REQUIRE(mapped.isOk());
  CHECK(mapped.value().nodes[0].transform == scene::Transform{});
  CHECK(mapped.value().nodes[0].camera == scene::Camera{});
  CHECK(mapped.value().nodes[1].light == scene::Light{});
}

TEST_CASE("authoring mapping: toAuthoringScene rejects unresolvable references", "[asset_system][scene][mapping]") {
  as::ParsedSceneSource source;
  source.nodes = {parsedNode(1, 1), parsedNode(2, 2)};
  scene::SceneMappingError expected{};

  SECTION("duplicate node_id") {
    source.nodes[1].nodeId = 1;
    expected = scene::SceneMappingError::DuplicateNodeId;
  }
  SECTION("ambiguous EntityGuid") {
    source.nodes[1].entityGuid = entity(1);
    expected = scene::SceneMappingError::AmbiguousEntityGuid;
  }
  SECTION("undeclared parent") {
    source.nodes[1].parentNodeId = 9;
    expected = scene::SceneMappingError::UndeclaredParentReference;
  }
  SECTION("undeclared active camera") {
    source.activeCameraNodeId = 9;
    expected = scene::SceneMappingError::UndeclaredActiveCameraReference;
  }
  SECTION("material without mesh") {
    source.nodes[0].materialAsset = asset(1);
    expected = scene::SceneMappingError::MaterialWithoutMesh;
  }

  const auto mapped = scene::toAuthoringScene(source);
  REQUIRE(mapped.isErr());
  CHECK(mapped.error() == expected);
}

TEST_CASE("authoring mapping: toParsedSceneSource rejects unresolvable references", "[asset_system][scene][mapping]") {
  scene::AuthoringScene s;
  s.nodes.resize(2);
  s.nodes[0].guid = entity(1);
  s.nodes[1].guid = entity(2);
  scene::SceneMappingError expected{};

  SECTION("ambiguous EntityGuid") {
    s.nodes[1].guid = entity(1);
    expected = scene::SceneMappingError::AmbiguousEntityGuid;
  }
  SECTION("dangling parent") {
    s.nodes[1].parent = entity(9);
    expected = scene::SceneMappingError::DanglingParentReference;
  }
  SECTION("dangling active camera") {
    s.activeCamera = entity(9);
    expected = scene::SceneMappingError::DanglingActiveCameraReference;
  }

  const auto mapped = scene::toParsedSceneSource(s);
  REQUIRE(mapped.isErr());
  CHECK(mapped.error() == expected);
}

TEST_CASE("authoring mapping: every committed scene source round-trips exactly", "[asset_system][scene][mapping]") {
  const auto files = corpus();
  REQUIRE(files.size() >= 28);  // 26 scenes, the fixture, the Bistro overlay
  for (const auto& file : files) {
    INFO(file.string());
    const auto parsed = as::parseSceneSource(readText(file));
    REQUIRE(parsed.isOk());
    const auto authored = scene::toAuthoringScene(parsed.value());
    REQUIRE(authored.isOk());
    const auto back = scene::toParsedSceneSource(authored.value());
    REQUIRE(back.isOk());
    REQUIRE(back.value().nodes.size() == parsed.value().nodes.size());
    for (std::size_t i = 0; i < back.value().nodes.size(); ++i) {
      CHECK(back.value().nodes[i].nodeId == i + 1);
      CHECK(back.value().nodes[i].entityGuid == parsed.value().nodes[i].entityGuid);
    }
    const auto again = scene::toAuthoringScene(back.value());
    REQUIRE(again.isOk());
    CHECK(again.value() == authored.value());
  }
}

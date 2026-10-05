// Plan 0047 M3 (P9; ADR-0097 D3/D5): scene source v7 carries guid= on every
// node and GUID mesh=/material= references; the artifact carries each node's
// EntityGuid; material source v10 names textures by GUID. Cook and decode
// each reject a nil or duplicate EntityGuid.

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/material_source.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_source.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace atlantis::asset_system;
namespace fs = std::filesystem;

namespace {

constexpr const char* kNodeGuidA = "0047bbbb-0000-4000-8000-000000000001";
constexpr const char* kNodeGuidB = "0047bbbb-0000-4000-8000-000000000002";
constexpr const char* kMeshGuid = "0047cccc-0000-4000-8000-000000000001";
constexpr const char* kNil = "00000000-0000-0000-0000-000000000000";

[[nodiscard]] std::string nodeLine(const std::string& nodeId, const std::string& guidToken,
                                   const std::string& tail = "") {
  return "node: node_id=" + nodeId + " " + guidToken +
         " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0" + tail + "\n";
}

[[nodiscard]] std::string scene(const std::vector<std::string>& nodeLines) {
  std::string text = "atlantis_scene_source_version: 7\nnode_count: " + std::to_string(nodeLines.size()) +
                     "\nactive_camera: none\n";
  for (const std::string& line : nodeLines) text += line;
  return text;
}

[[nodiscard]] std::string material(const std::string& textureValue) {
  return "atlantis_material_source_version: 10\n"
         "kind: unlit_textured\n"
         "texture: " + textureValue + "\n"
         "filter: linear\n"
         "address_mode: repeat\n";
}

// A scratch directory removed on destruction.
struct Scratch {
  fs::path path = fs::temp_directory_path() / "atlantis_guid_reference_tests";
  Scratch() {
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
};

[[nodiscard]] atlantis::Result<std::monostate, SceneCookError> cookText(const Scratch& scratch,
                                                                       const std::string& text) {
  std::ofstream(scratch.path / "s.scene.txt", std::ios::binary) << text;
  return cookScene((scratch.path / "s.scene.txt").string(),
                   parseAssetGuid("0047dddd-0000-4000-8000-000000000001").value(),
                   (scratch.path / "s.ascene").string(), (scratch.path / "s.ascene.meta.txt").string());
}

}  // namespace

TEST_CASE("Scene source v7 reads guid= and GUID mesh= references", "[asset_system][scene][guid]") {
  const auto parsed =
      parseSceneSource(scene({nodeLine("1", std::string("guid=") + kNodeGuidA, std::string(" mesh=") + kMeshGuid)}));
  REQUIRE(parsed.isOk());
  CHECK(toString(parsed.value().nodes[0].entityGuid) == kNodeGuidA);
  REQUIRE(parsed.value().nodes[0].meshAsset.has_value());
  CHECK(toString(*parsed.value().nodes[0].meshAsset) == kMeshGuid);

  const std::string reserialized = serializeSceneSource(parsed.value());
  const auto reparsed = parseSceneSource(reserialized);
  REQUIRE(reparsed.isOk());
  CHECK(reparsed.value().nodes[0].entityGuid == parsed.value().nodes[0].entityGuid);
  CHECK(reparsed.value().nodes[0].meshAsset == parsed.value().nodes[0].meshAsset);
}

TEST_CASE("Scene source v7 rejects a missing or malformed guid= and malformed references",
          "[asset_system][scene][guid]") {
  CHECK(parseSceneSource(scene({nodeLine("1", "parent_marker=1")})).error() ==
        SceneSourceParseError::FieldOrderMismatch);
  CHECK(parseSceneSource(scene({"node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 "
                                "1.0 1.0\n"}))
            .error() == SceneSourceParseError::FieldOrderMismatch);
  CHECK(parseSceneSource(scene({nodeLine("1", "guid=not-a-guid")})).error() == SceneSourceParseError::MalformedGuid);
  CHECK(parseSceneSource(scene({nodeLine("1", std::string("guid=") + kNodeGuidA, " mesh=meshes/cube.mesh.txt")}))
            .error() == SceneSourceParseError::MalformedGuid);
  CHECK(parseSceneSource(scene({nodeLine("1", std::string("guid=") + kNodeGuidA, std::string(" mesh=") + kNil)}))
            .error() == SceneSourceParseError::MalformedGuid);
  CHECK(parseSceneSource(scene({nodeLine("1", std::string("guid=") + kNodeGuidA,
                                         std::string(" mesh=") + kMeshGuid + " material=materials/a.material.txt")}))
            .error() == SceneSourceParseError::MalformedGuid);
}

TEST_CASE("A nil guid= parses, and cookScene rejects it as NilEntityGuid", "[asset_system][scene][guid]") {
  const std::string text = scene({nodeLine("1", std::string("guid=") + kNil)});
  const auto parsed = parseSceneSource(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().nodes[0].entityGuid == EntityGuid{});

  const Scratch scratch;
  CHECK(cookText(scratch, text).error() == SceneCookError::NilEntityGuid);
  CHECK_FALSE(fs::exists(scratch.path / "s.ascene"));
}

TEST_CASE("cookScene rejects two nodes sharing one guid= as DuplicateEntityGuid", "[asset_system][scene][guid]") {
  const Scratch scratch;
  const std::string guidToken = std::string("guid=") + kNodeGuidA;
  CHECK(cookText(scratch, scene({nodeLine("1", guidToken), nodeLine("2", guidToken)})).error() ==
        SceneCookError::DuplicateEntityGuid);
  CHECK_FALSE(fs::exists(scratch.path / "s.ascene"));
}

TEST_CASE("The scene artifact carries each node's EntityGuid through cook and decode", "[asset_system][scene][guid]") {
  const Scratch scratch;
  REQUIRE(cookText(scratch, scene({nodeLine("1", std::string("guid=") + kNodeGuidA),
                                   nodeLine("2", std::string("guid=") + kNodeGuidB)}))
              .isOk());
  std::ifstream in(scratch.path / "s.ascene", std::ios::binary);
  const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(raw[i]);

  const auto decoded = decodeSceneArtifact(bytes);
  REQUIRE(decoded.isOk());
  REQUIRE(decoded.value().entityGuids.size() == 2);
  CHECK(toString(decoded.value().entityGuids[0]) == kNodeGuidA);
  CHECK(toString(decoded.value().entityGuids[1]) == kNodeGuidB);
}

TEST_CASE("decodeSceneArtifact rejects a nil or duplicate EntityGuid, never trusting the cooker",
          "[asset_system][scene][guid]") {
  const std::vector<ValidatedSceneNode> nodes(2);
  const std::vector<std::optional<std::size_t>> parents(2);
  const EntityGuid a = parseEntityGuid(kNodeGuidA).value();

  const auto nilBytes = encodeSceneArtifact(nodes, parents, std::nullopt, {a, EntityGuid{}});
  CHECK(decodeSceneArtifact(nilBytes).error() == SceneArtifactDecodeError::NilEntityGuid);

  const auto duplicateBytes = encodeSceneArtifact(nodes, parents, std::nullopt, {a, a});
  CHECK(decodeSceneArtifact(duplicateBytes).error() == SceneArtifactDecodeError::DuplicateEntityGuid);
}

TEST_CASE("Material source v10 names its textures by GUID", "[asset_system][material][guid]") {
  const auto parsed = parseMaterialSource(material(kMeshGuid));
  REQUIRE(parsed.isOk());
  CHECK(toString(parsed.value().textureAsset) == kMeshGuid);
  CHECK(parseMaterialSource(serializeMaterialSource(parsed.value())).value().textureAsset ==
        parsed.value().textureAsset);

  CHECK(parseMaterialSource(material("textures/a.png")).error() == MaterialSourceParseError::MalformedGuid);
  CHECK(parseMaterialSource(material(kNil)).error() == MaterialSourceParseError::MalformedGuid);
  CHECK(parseMaterialSource(material("")).error() == MaterialSourceParseError::MissingField);
}

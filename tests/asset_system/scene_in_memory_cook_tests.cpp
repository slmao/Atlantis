#include <atlantis/asset_system/cook_scene.h>

#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_types.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>

// Plan 0065 M1 (P1; Spec 0065 R7, R9 and its Correction of 2026-10-22; ADR-0131
// D2 and its amendment): the in-memory cook is the build's own text path, so its
// artifact equals the build's file byte for byte, and its decode equals the file
// decode. A document its text cannot represent is refused, never narrowed.

namespace {

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDir {
  fs::path path = fs::temp_directory_path() / "atlantis_scene_in_memory_cook_tests" /
                  (gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

[[nodiscard]] std::vector<std::byte> readBytes(const fs::path& path) {
  const std::string text = readText(path);
  std::vector<std::byte> bytes(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) bytes[i] = static_cast<std::byte>(text[i]);
  return bytes;
}

[[nodiscard]] as::EntityGuid entity(std::uint32_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xe6};
  bytes[14] = static_cast<std::byte>((n >> 8) & 0xffu);
  bytes[15] = static_cast<std::byte>(n & 0xffu);
  return as::entityGuidFromBytes(bytes).value();
}

[[nodiscard]] as::AssetGuid sceneGuid() { return as::parseAssetGuid("00650065-0065-4065-8065-006500650065").value(); }

// A scene the build cooked: its committed (or generated) source, and the
// artifact and sidecar the asset cooker wrote from it with cookScene().
struct BuildCookedScene {
  fs::path source;
  fs::path artifact;
  fs::path metadata;
};

[[nodiscard]] std::vector<BuildCookedScene> buildCookedScenes() {
  std::vector<BuildCookedScene> out;
  const fs::path assets{ATLANTIS_ASSETS_DIR};
  for (const auto& entry : fs::directory_iterator(assets / "scenes")) {
    const std::string name = entry.path().filename().string();
    if (!name.ends_with(".scene.txt")) continue;
    const std::string stem = name.substr(0, name.size() - std::string_view{".scene.txt"}.size());
    const fs::path artifact = fs::path{ATLANTIS_COOKED_SCENES_DIR} / (stem + ".ascene");
    out.push_back({entry.path(), artifact, fs::path{artifact.string() + ".meta.txt"}});
  }
  out.push_back({assets / "_test_fixtures" / "cmake_scene_declaration_test.scene.txt",
                 fs::path{ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH},
                 fs::path{ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH}});
  return out;
}

[[nodiscard]] scene::AuthoringScene documentOf(const std::string& text) {
  const auto parsed = as::parseSceneSource(text);
  REQUIRE(parsed.isOk());
  const auto document = scene::toAuthoringScene(parsed.value());
  REQUIRE(document.isOk());
  return document.value();
}

// Everything a ValidatedSceneData carries, bit for bit: re-encoded through the
// artifact codec, which writes every field of it.
[[nodiscard]] std::vector<std::byte> reencoded(const as::ValidatedSceneData& data) {
  std::vector<as::ValidatedSceneNode> nodes;
  std::vector<std::optional<std::size_t>> parents;
  std::vector<as::EntityGuid> guids;
  for (std::size_t i = 0; i < data.nodeCount(); ++i) {
    nodes.push_back(data.node(i));
    parents.push_back(data.parentOf(i));
    guids.push_back(data.entityGuid(i));
  }
  return as::encodeSceneArtifact(nodes, parents, data.activeCameraIndex(), guids);
}

// The in-memory cook equals the build's file cook, from the source file and
// from the saved text, and the in-memory decode equals the file decode.
void checkInMemoryEqualsBuild(const BuildCookedScene& built) {
  INFO(built.source.string());
  REQUIRE(fs::exists(built.artifact));
  REQUIRE(fs::exists(built.metadata));
  const std::string sourceText = readText(built.source);
  const std::vector<std::byte> fileArtifact = readBytes(built.artifact);
  const std::string fileMetadata = readText(built.metadata);
  const auto sidecar = as::parseSceneMetadata(fileMetadata);
  REQUIRE(sidecar.isOk());
  const as::AssetGuid guid = sidecar.value().assetGuid;

  // From the source file.
  const auto fromSource = as::cookSceneSource(sourceText, guid);
  REQUIRE(fromSource.isOk());
  CHECK(fromSource.value().artifactBytes == fileArtifact);
  CHECK(fromSource.value().metadataText == fileMetadata);

  // From the saved text: the document, cooked in memory (serialize, parse, cook).
  const scene::AuthoringScene document = documentOf(sourceText);
  const auto fromDocument = as::cookSceneToMemory(document, guid);
  REQUIRE(fromDocument.isOk());
  CHECK(fromDocument.value().artifactBytes == fileArtifact);
  CHECK(fromDocument.value().metadataText == fileMetadata);

  // ... and the saved text written to a file and cooked by cookScene().
  const auto mapped = scene::toParsedSceneSource(document);
  REQUIRE(mapped.isOk());
  TempDir dir;
  const fs::path saved = dir.path / "saved.scene.txt";
  std::ofstream(saved, std::ios::binary) << as::serializeSceneSource(mapped.value());
  const auto fileCook = as::cookScene(saved.string(), guid, (dir.path / "saved.ascene").string(),
                                      (dir.path / "saved.ascene.meta.txt").string());
  REQUIRE(fileCook.isOk());
  CHECK(readBytes(dir.path / "saved.ascene") == fileArtifact);
  CHECK(readText(dir.path / "saved.ascene.meta.txt") == fileMetadata);

  // In-memory decode = file decode.
  const auto memoryDecoded = as::decodeScene(fromDocument.value());
  const auto fileDecoded = as::decodeScene(built.artifact.string(), built.metadata.string());
  REQUIRE(memoryDecoded.isOk());
  REQUIRE(fileDecoded.isOk());
  CHECK(reencoded(memoryDecoded.value()) == reencoded(fileDecoded.value()));
  CHECK(reencoded(memoryDecoded.value()) == fileArtifact);
}

[[nodiscard]] scene::AuthoringNode cameraNode(std::uint32_t n) {
  scene::AuthoringNode node;
  node.guid = entity(n);
  scene::Camera camera;
  camera.fovYRadians = 1.0472f;
  camera.nearZ = 0.1f;
  camera.farZ = 100.0f;
  node.camera = camera;
  return node;
}

[[nodiscard]] scene::AuthoringNode pointLightNode(std::uint32_t n) {
  scene::AuthoringNode node;
  node.guid = entity(n);
  scene::Light light;
  light.kind = scene::LightKind::Point;
  light.color = {1.0f, 0.85f, 0.6f};
  light.intensity = 4.5f;
  light.range = 12.0f;
  node.light = light;
  return node;
}

[[nodiscard]] scene::AuthoringScene oneCameraScene() {
  scene::AuthoringScene document;
  document.nodes.push_back(cameraNode(1));
  document.activeCamera = entity(1);
  return document;
}

}  // namespace

// ================================================== in-memory = build, every scene

TEST_CASE("in-memory cook: every committed scene cooks to the build's artifact and sidecar, byte for byte",
          "[asset_system][scene][in_memory_cook]") {
  const auto scenes = buildCookedScenes();
  REQUIRE(scenes.size() == 27);  // 26 scenes and the CMake-declaration fixture
  for (const BuildCookedScene& built : scenes) checkInMemoryEqualsBuild(built);
}

#if defined(ATLANTIS_BISTRO_SCENE_IMPORT_DIR)
TEST_CASE("in-memory cook: Bistro's generated source cooks to the build's artifact and sidecar, byte for byte",
          "[asset_system][scene][in_memory_cook][content]") {
  const BuildCookedScene bistro{fs::path{ATLANTIS_BISTRO_SCENE_IMPORT_DIR} / "bistro" / "bistro.scene.txt",
                                fs::path{ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH},
                                fs::path{std::string{ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH} + ".meta.txt"}};
  if (!fs::exists(bistro.source) || !fs::exists(bistro.artifact)) SKIP("Bistro content not imported");
  checkInMemoryEqualsBuild(bistro);
}
#endif

// ======================================================== the cook's own refusals

TEST_CASE("in-memory cook: a parse-level violation is refused, as the build refuses it",
          "[asset_system][scene][in_memory_cook]") {
  SECTION("a point light beyond the grammar's limit") {
    scene::AuthoringScene document = oneCameraScene();
    for (std::uint32_t i = 0; i <= as::kMaxPointLightsPerScene; ++i) document.nodes.push_back(pointLightNode(2 + i));
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::SourceParseFailed);

    document.nodes.pop_back();  // exactly at the limit: cooks
    CHECK(as::cookSceneToMemory(document, sceneGuid()).isOk());
  }
  SECTION("a second directional light") {
    scene::AuthoringScene document = oneCameraScene();
    for (std::uint32_t n : {2u, 3u}) {
      scene::AuthoringNode node;
      node.guid = entity(n);
      node.light = scene::Light{};
      document.nodes.push_back(node);
    }
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::SourceParseFailed);
  }
  SECTION("a light value outside the grammar's domain") {
    scene::AuthoringScene document = oneCameraScene();
    scene::AuthoringNode light = pointLightNode(2);
    light.light->range = 0.0f;
    document.nodes.push_back(light);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::SourceParseFailed);
  }
}

TEST_CASE("in-memory cook: the cooker's checks refuse as cookScene() does", "[asset_system][scene][in_memory_cook]") {
  SECTION("an empty document") {
    const auto result = as::cookSceneToMemory(scene::AuthoringScene{}, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::EmptyScene);
  }
  SECTION("a non-finite value is NonFiniteValue, checked before the round trip") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes[0].camera->fovYRadians = std::numeric_limits<float>::quiet_NaN();
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::NonFiniteValue);
  }
  SECTION("a fog value outside its domain") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes[0].camera->fog.maxOpacity = 2.0f;
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::NonFiniteValue);
  }
  SECTION("an active camera without a camera") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes.push_back(pointLightNode(2));
    document.activeCamera = entity(2);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::ActiveCameraMissingCamera);
  }
  SECTION("a parent cycle") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes.push_back(pointLightNode(2));
    document.nodes[0].parent = entity(2);
    document.nodes[1].parent = entity(1);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::ParentCycle);
  }
}

TEST_CASE("in-memory cook: the mapping's errors fold into SceneCookError", "[asset_system][scene][in_memory_cook]") {
  SECTION("a parent GUID naming no node is UndeclaredParentReference") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes[0].parent = entity(99);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::UndeclaredParentReference);
  }
  SECTION("an active-camera GUID naming no node is UndeclaredActiveCameraReference") {
    scene::AuthoringScene document = oneCameraScene();
    document.activeCamera = entity(99);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::UndeclaredActiveCameraReference);
  }
  SECTION("two nodes sharing an EntityGuid is DuplicateEntityGuid") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes.push_back(pointLightNode(1));
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::DuplicateEntityGuid);
  }
}

// ============================================================ the round-trip check

TEST_CASE("in-memory cook: a document its text cannot represent is NotRepresentableInSource, never narrowed",
          "[asset_system][scene][in_memory_cook]") {
  SECTION("a camera and a light on one node") {
    scene::AuthoringScene document = oneCameraScene();
    document.nodes[0].light = pointLightNode(1).light;
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::NotRepresentableInSource);
  }
  SECTION("a mesh and a light on one node") {
    scene::AuthoringScene document = oneCameraScene();
    scene::AuthoringNode node = pointLightNode(2);
    node.renderable = scene::Renderable{as::parseAssetGuid("00650065-0065-4065-8065-00650065a001").value(), {}};
    document.nodes.push_back(node);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::NotRepresentableInSource);
  }
  SECTION("a directional light with a range") {
    scene::AuthoringScene document = oneCameraScene();
    scene::AuthoringNode node;
    node.guid = entity(2);
    node.light = scene::Light{scene::LightKind::Directional, {1.0f, 1.0f, 1.0f}, 1.0f, 5.0f};
    document.nodes.push_back(node);
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::NotRepresentableInSource);
  }
  SECTION("a fog group whose seven values are all zero (it is not written, and reads back as the defaults)") {
    // Spec 0065's Correction writes the fog group when any of its values is
    // non-zero. An all-zero group is therefore omitted, and the parser fills
    // the no-group defaults (colour 1, max opacity 1) -- refused here.
    scene::AuthoringScene document = oneCameraScene();
    document.nodes[0].camera->fog = scene::CameraFog{{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f};
    const auto result = as::cookSceneToMemory(document, sceneGuid());
    REQUIRE(result.isErr());
    CHECK(result.error() == as::SceneCookError::NotRepresentableInSource);
  }
}

TEST_CASE("in-memory cook: off-but-valued fog and bloom groups and hard floats cook, and decode back exactly",
          "[asset_system][scene][in_memory_cook]") {
  scene::AuthoringScene document = oneCameraScene();
  document.nodes[0].camera->fog = scene::CameraFog{{0.7f, 0.0f, 0.8f}, 0.0f, 2.0f, 0.0f, 0.8f};
  document.nodes[0].camera->bloom = scene::CameraBloom{0.0f, 3.0f};
  document.nodes[0].camera->exposureCompensationEv = -0.0f;
  document.nodes[0].transform.localPosition = {std::numeric_limits<float>::denorm_min(), 1e30f, -0.0f};
  document.nodes[0].transform.localEulerAnglesRadians = {0.1234567f, -0.6435011f, 1e-7f};
  scene::AuthoringNode light = pointLightNode(2);
  light.light->color = {std::numeric_limits<float>::denorm_min(), 0.1234567f, 1.0f};
  light.light->intensity = 1e30f;
  document.nodes.push_back(light);

  const auto cooked = as::cookSceneToMemory(document, sceneGuid());
  REQUIRE(cooked.isOk());
  const auto decoded = as::decodeScene(cooked.value());
  REQUIRE(decoded.isOk());
  REQUIRE(decoded.value().nodeCount() == 2);

  const as::ValidatedSceneNode& camera = decoded.value().node(0);
  REQUIRE(camera.camera.has_value());
  CHECK(camera.camera->fog.density == 0.0f);
  CHECK(camera.camera->fog.height == 2.0f);
  CHECK(camera.camera->fog.maxOpacity == 0.8f);
  CHECK(camera.camera->fog.colorR == 0.7f);
  CHECK(camera.camera->fog.colorG == 0.0f);
  CHECK(camera.camera->bloom.strength == 0.0f);
  CHECK(camera.camera->bloom.threshold == 3.0f);
  CHECK(std::signbit(camera.camera->exposureCompensationEv));
  CHECK(camera.transform.positionX == std::numeric_limits<float>::denorm_min());
  CHECK(camera.transform.positionY == 1e30f);
  CHECK(std::signbit(camera.transform.positionZ));
  CHECK(camera.transform.eulerXRadians == 0.1234567f);
  CHECK(camera.transform.eulerYRadians == -0.6435011f);
  CHECK(camera.transform.eulerZRadians == 1e-7f);

  const as::ValidatedSceneNode& point = decoded.value().node(1);
  REQUIRE(point.light.has_value());
  CHECK(point.light->colorR == std::numeric_limits<float>::denorm_min());
  CHECK(point.light->colorG == 0.1234567f);
  CHECK(point.light->intensity == 1e30f);
}

// ===================================================== cookScene() over the split

TEST_CASE("cookScene() writes exactly what cookSceneSource() returns", "[asset_system][scene][in_memory_cook]") {
  const std::string text = std::string{"atlantis_scene_source_version: 7\n"} +
                           "node_count: 1\n"
                           "active_camera: 1\n"
                           "node: node_id=1 guid=e68122c6-18b2-8f1f-b185-358f58780754 parent=none position=0.0 2.2 "
                           "7.0 rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 "
                           "camera_far_z=100.0\n";
  const auto inMemory = as::cookSceneSource(text, sceneGuid());
  REQUIRE(inMemory.isOk());

  TempDir dir;
  std::ofstream(dir.path / "s.scene.txt", std::ios::binary) << text;
  const auto written = as::cookScene((dir.path / "s.scene.txt").string(), sceneGuid(), (dir.path / "s.ascene").string(),
                                     (dir.path / "s.ascene.meta.txt").string());
  REQUIRE(written.isOk());
  CHECK(readBytes(dir.path / "s.ascene") == inMemory.value().artifactBytes);
  CHECK(readText(dir.path / "s.ascene.meta.txt") == inMemory.value().metadataText);

  const auto refused = as::cookSceneSource("not a scene", sceneGuid());
  REQUIRE(refused.isErr());
  CHECK(refused.error() == as::SceneCookError::SourceParseFailed);
}

#include <atlantis/asset_system/decode_scene.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/scene_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_decode_scene_tests" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// A hand-built, always-structurally-plausible two-node scene, encoded
// directly via encodeSceneArtifact() -- bypassing cookScene() and its
// own validation entirely, matching V9/V28's own "decode-time-injected"
// and "bypassing the cooker entirely" language. Node 0: a Renderable,
// no parent. Node 1: a Camera, parent = node 0, active camera.
[[nodiscard]] std::vector<ValidatedSceneNode> makeTwoNodes() {
  ValidatedSceneNode node0;
  node0.transform = {1.0f, 2.0f, 3.0f, 0.1f, 0.2f, 0.3f, 1.0f, 1.0f, 1.0f};
  node0.renderable = DecodedRenderable{0x0102030405060708ULL};

  ValidatedSceneNode node1;
  node1.transform = {4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  node1.camera = DecodedCamera{1.0472f, 0.1f, 100.0f};

  return {node0, node1};
}

constexpr std::string_view kValidTwoNodeTextSource =
    "atlantis_scene_source_version: 1\n"
    "node_count: 2\n"
    "active_camera: 2\n"
    "node: node_id=1 parent=none position=1.0 2.0 3.0 rotation=0.1 0.2 0.3 scale=1.0 1.0 1.0 "
    "mesh=meshes/minimal_cube.mesh.txt\n"
    "node: node_id=2 parent=1 position=4.0 5.0 6.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n";

}  // namespace

// ---------------------------------------------------------------------
// V8: full round-trip through cookScene() + decodeScene(), every field.
// ---------------------------------------------------------------------

TEST_CASE("decodeScene reproduces every field cookScene() encoded (V8)", "[asset_system][scene]") {
  TempDirGuard dir("roundtrip");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  const fs::path metadataPath = dir.path / "scene.ascene.meta.txt";
  writeFile(sourcePath, std::string(kValidTwoNodeTextSource));

  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());

  const auto result = decodeScene(artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());
  const ValidatedSceneData& scene = result.value();

  REQUIRE(scene.nodeCount() == 2);

  CHECK(scene.node(0).transform.positionX == 1.0f);
  CHECK(scene.node(0).transform.positionY == 2.0f);
  CHECK(scene.node(0).transform.positionZ == 3.0f);
  CHECK(scene.node(0).transform.eulerXRadians == 0.1f);
  CHECK(scene.node(0).transform.eulerYRadians == 0.2f);
  CHECK(scene.node(0).transform.eulerZRadians == 0.3f);
  CHECK(scene.node(0).transform.scaleX == 1.0f);
  CHECK_FALSE(scene.node(0).camera.has_value());
  REQUIRE(scene.node(0).renderable.has_value());
  CHECK(scene.node(0).renderable->meshAsset == computeAssetId(normalizeLogicalPath("meshes/minimal_cube.mesh.txt").value()));
  CHECK_FALSE(scene.parentOf(0).has_value());

  CHECK(scene.node(1).transform.positionX == 4.0f);
  CHECK(scene.node(1).transform.positionY == 5.0f);
  CHECK(scene.node(1).transform.positionZ == 6.0f);
  REQUIRE(scene.node(1).camera.has_value());
  CHECK(scene.node(1).camera->fovYRadians == 1.0472f);
  CHECK(scene.node(1).camera->nearZ == 0.1f);
  CHECK(scene.node(1).camera->farZ == 100.0f);
  CHECK_FALSE(scene.node(1).renderable.has_value());
  REQUIRE(scene.parentOf(1).has_value());
  CHECK(*scene.parentOf(1) == 0);

  REQUIRE(scene.activeCameraIndex().has_value());
  CHECK(*scene.activeCameraIndex() == 1);
}

// ---------------------------------------------------------------------
// V9: every SceneArtifactDecodeError condition, individually triggered.
// ---------------------------------------------------------------------

TEST_CASE("decodeSceneArtifact rejects a buffer too small for the header", "[asset_system][scene]") {
  const std::vector<std::byte> tooSmall(10, std::byte{0});
  const auto result = decodeSceneArtifact(tooSmall);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::TooSmallForHeader);
}

TEST_CASE("decodeSceneArtifact rejects a bad magic", "[asset_system][scene]") {
  auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  bytes[0] = std::byte{0x00};
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeSceneArtifact rejects an unknown schema version", "[asset_system][scene]") {
  auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  bytes[4] = std::byte{0x02};  // schema_version's low byte, offset 4
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeSceneArtifact rejects a truncated buffer (size mismatch)", "[asset_system][scene]") {
  auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  bytes.pop_back();
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::SizeMismatch);
}

TEST_CASE("decodeSceneArtifact rejects an out-of-range parent index", "[asset_system][scene]") {
  // parents[1] = 99: no such node in a two-node scene.
  const auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, std::size_t{99}}, 1);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::OutOfRangeParentIndex);
}

TEST_CASE("decodeSceneArtifact rejects a decode-time-injected parent cycle", "[asset_system][scene]") {
  // Both node 0 and node 1 name the other as parent -- a 2-cycle no
  // cookScene() would ever produce, constructed here by calling the
  // codec directly, bypassing the cooker entirely.
  const auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::size_t{1}, std::size_t{0}}, std::nullopt);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::CyclicParent);
}

TEST_CASE("decodeSceneArtifact rejects an out-of-range active-camera index", "[asset_system][scene]") {
  const auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, std::size_t{99});
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::OutOfRangeActiveCameraIndex);
}

TEST_CASE("decodeSceneArtifact rejects a decode-time-injected active-camera-missing-Camera case",
          "[asset_system][scene]") {
  // Node 0 has a Renderable, not a Camera -- pointing active_camera at
  // it directly via the codec (never reachable through cookScene()'s
  // own D4 step 6) exercises this decode-side re-check independently.
  const auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, std::size_t{0});
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::ActiveCameraMissingCamera);
}

TEST_CASE("decodeSceneArtifact rejects a non-finite transform value", "[asset_system][scene]") {
  auto nodes = makeTwoNodes();
  nodes[0].transform.positionX = std::nanf("");
  const auto bytes = encodeSceneArtifact(nodes, {std::nullopt, 0}, 1);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::NonFiniteValue);
}

TEST_CASE("decodeSceneArtifact rejects a non-finite camera value", "[asset_system][scene]") {
  auto nodes = makeTwoNodes();
  nodes[1].camera->nearZ = std::numeric_limits<float>::infinity();
  const auto bytes = encodeSceneArtifact(nodes, {std::nullopt, 0}, 1);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::NonFiniteValue);
}

TEST_CASE("decodeScene rejects an unreadable artifact file", "[asset_system][scene]") {
  TempDirGuard dir("artifact_unreadable");
  writeFile(dir.path / "scene.ascene.meta.txt",
            "atlantis_scene_metadata_version: 1\nschema_version: 1\nnode_count: 1\n");
  const auto result =
      decodeScene((dir.path / "does_not_exist.ascene").string(), (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::ArtifactUnreadable);
}

TEST_CASE("decodeScene rejects an unreadable metadata file", "[asset_system][scene]") {
  TempDirGuard dir("metadata_unreadable");
  const auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  const fs::path artifactPath = dir.path / "scene.ascene";
  {
    std::ofstream out(artifactPath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto result = decodeScene(artifactPath.string(), (dir.path / "does_not_exist.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::MetadataUnreadable);
}

TEST_CASE("decodeScene rejects a malformed metadata file", "[asset_system][scene]") {
  TempDirGuard dir("metadata_malformed");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  const fs::path metadataPath = dir.path / "scene.ascene.meta.txt";
  writeFile(sourcePath, std::string(kValidTwoNodeTextSource));
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());

  writeFile(metadataPath, "not valid metadata\n");
  const auto result = decodeScene(artifactPath.string(), metadataPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::MetadataParseFailed);
}

TEST_CASE("decodeScene rejects a metadata/artifact node_count mismatch", "[asset_system][scene]") {
  TempDirGuard dir("metadata_mismatch");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  const fs::path metadataPath = dir.path / "scene.ascene.meta.txt";
  writeFile(sourcePath, std::string(kValidTwoNodeTextSource));
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());

  writeFile(metadataPath, "atlantis_scene_metadata_version: 1\nschema_version: 1\nnode_count: 99\n");
  const auto result = decodeScene(artifactPath.string(), metadataPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::MetadataArtifactMismatch);
}

// ---------------------------------------------------------------------
// V10: implausibly large node_count rejected before allocation.
// ---------------------------------------------------------------------

TEST_CASE("decodeSceneArtifact rejects an implausibly large node_count before allocating (V10)",
          "[asset_system][scene]") {
  std::vector<std::byte> bytes(24, std::byte{0});
  bytes[0] = std::byte{'A'};
  bytes[1] = std::byte{'S'};
  bytes[2] = std::byte{'C'};
  bytes[3] = std::byte{'N'};
  bytes[4] = std::byte{0x01};  // schema_version = 1
  // node_count at offset 8, a huge value: 0xFFFFFFFF.
  bytes[8] = std::byte{0xFF};
  bytes[9] = std::byte{0xFF};
  bytes[10] = std::byte{0xFF};
  bytes[11] = std::byte{0xFF};

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::NodeCountOutOfRange);
}

// ---------------------------------------------------------------------
// V28 (decode-side): a hand-crafted, artificially-empty artifact is
// rejected independently of the cooker.
// ---------------------------------------------------------------------

TEST_CASE("decodeSceneArtifact rejects a hand-crafted empty artifact (V28, decode-side)", "[asset_system][scene]") {
  const auto bytes = encodeSceneArtifact({}, {}, std::nullopt);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::EmptyScene);
}

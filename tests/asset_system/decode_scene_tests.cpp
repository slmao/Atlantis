#include <atlantis/asset_system/decode_scene.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/scene_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <bit>
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
    "atlantis_scene_source_version: 3\n"
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
  // Plan 0019: this literal must name a value still genuinely
  // unrecognized now that version 3 is the real, accepted version -- 4
  // here, not 3 (matching Plan 0020's own identical precedent).
  auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  bytes[4] = std::byte{0x04};  // schema_version's low byte, offset 4
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeSceneArtifact rejects the superseded schema version 1 outright", "[asset_system][scene]") {
  // Plan 0018 Section P7 / Spec 0018 D5: version 1 is rejected outright
  // once the material slot (version 2) exists -- no dual-version reader.
  // Unchanged by Plan 0019: version 1 stays rejected under version 3's
  // own check exactly as it was under version 2's.
  auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  bytes[4] = std::byte{0x01};
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::UnknownSchemaVersion);
}

TEST_CASE("decodeSceneArtifact rejects the superseded schema version 2 outright", "[asset_system][scene]") {
  // Plan 0019 Section P4: version 2 (pre-light, no light slot) is now
  // also rejected outright, exactly like version 1 already was.
  auto bytes = encodeSceneArtifact(makeTwoNodes(), {std::nullopt, 0}, 1);
  bytes[4] = std::byte{0x02};
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
  bytes[4] = std::byte{0x03};  // schema_version = 3 (Plan 0019 P4)
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
// Plan 0018 Section P7: the new material slot.
// ---------------------------------------------------------------------

TEST_CASE("encodeSceneArtifact then decodeSceneArtifact round-trips a node's materialAsset",
          "[asset_system][scene][material]") {
  ValidatedSceneNode node;
  node.transform = {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  node.renderable = DecodedRenderable{0x0102030405060708ULL, std::optional<AssetId>(0x1122334455667788ULL)};

  const auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  REQUIRE(bytes.size() == kSceneArtifactHeaderSizeBytes + kSceneArtifactNodeRecordSizeBytes);

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].renderable.has_value());
  CHECK(result.value().nodes[0].renderable->meshAsset == 0x0102030405060708ULL);
  REQUIRE(result.value().nodes[0].renderable->materialAsset.has_value());
  CHECK(*result.value().nodes[0].renderable->materialAsset == 0x1122334455667788ULL);
}

TEST_CASE("encodeSceneArtifact then decodeSceneArtifact round-trips a renderable with no material",
          "[asset_system][scene][material]") {
  ValidatedSceneNode node;
  node.transform = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  node.renderable = DecodedRenderable{0x0102030405060708ULL};

  const auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].renderable.has_value());
  CHECK_FALSE(result.value().nodes[0].renderable->materialAsset.has_value());
}

TEST_CASE("decodeSceneArtifact rejects a hand-crafted material-without-renderable record",
          "[asset_system][scene][material]") {
  // cookScene() itself can never produce this (Plan 0018 Section P6's
  // own grammar-structural guarantee) -- this proves
  // decodeSceneArtifact()'s own independent, never-trust-the-cooker
  // check catches it anyway.
  ValidatedSceneNode node;
  node.transform = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  // No renderable set -- has_renderable will encode as 0.
  auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);

  // Corrupt: set has_material (offset 24 within the one node record,
  // i.e. kSceneArtifactHeaderSizeBytes + 64) to 1, leaving
  // has_renderable (offset 52 within the record) at 0.
  const std::size_t hasMaterialOffset = kSceneArtifactHeaderSizeBytes + 64;
  bytes[hasMaterialOffset] = std::byte{0x01};

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::MaterialWithoutRenderable);
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

// ---------------------------------------------------------------------
// Plan 0019 Milestone 3 (P4): the light slot's own exact byte layout,
// pinned-byte encode test, and independent decode-time re-validation.
// ---------------------------------------------------------------------

TEST_CASE("encodeSceneArtifact matches an independently-computed expected byte vector for a one-node, "
          "light-bearing scene",
          "[asset_system][scene][light]") {
  // Pins the little-endian contract at the new 112-byte node stride
  // (Plan 0019 Section P4: a light slot inserted after material, before
  // parent). Independently computed (.NET's own BitConverter.GetBytes(),
  // not transcribed from memory), matching mesh_artifact_tests.cpp's own
  // established discipline exactly -- this test never calls
  // encodeSceneArtifact() to produce its own expected value.
  ValidatedSceneNode node;
  node.transform = {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  node.light = DecodedLight{DecodedLightKind::Directional, 0.5f, 0.25f, 0.75f, 2.0f, 0.0f};

  const std::vector<std::byte> expected = {
      // Magic "ASCN"
      std::byte{0x41}, std::byte{0x53}, std::byte{0x43}, std::byte{0x4E},
      // schema_version = 3
      std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // node_count = 1
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // has_active_camera = 0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // active_camera_index = 0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // reserved = 0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // position (1.0, 2.0, 3.0)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x40},
      // rotation (0, 0, 0)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // scale (1.0, 1.0, 1.0)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},
      // has_camera = 0, fov_y/near_z/far_z = 0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // has_renderable = 0, mesh_asset_id = 0 (u64)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // has_material = 0, material_asset_id = 0 (u64)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // has_light = 1
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // light_kind = 0 (Directional)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // color (0.5, 0.25, 0.75)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x80}, std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x3F},
      // intensity = 2.0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
      // range = 0.0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      // has_parent = 0, parent_index = 0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00},
  };
  REQUIRE(expected.size() == 136);  // 24-byte header + 112-byte node record

  const std::vector<std::byte> actual = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  CHECK(actual == expected);
}

TEST_CASE("decodeSceneArtifact round-trips a light-bearing node through encode+decode", "[asset_system][scene][light]") {
  ValidatedSceneNode node;
  node.transform = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  node.light = DecodedLight{DecodedLightKind::Point, 0.1f, 0.2f, 0.3f, 4.0f, 12.5f};

  const auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes.size() == 1);
  REQUIRE(result.value().nodes[0].light.has_value());
  CHECK(result.value().nodes[0].light->kind == DecodedLightKind::Point);
  CHECK(result.value().nodes[0].light->colorR == 0.1f);
  CHECK(result.value().nodes[0].light->colorG == 0.2f);
  CHECK(result.value().nodes[0].light->colorB == 0.3f);
  CHECK(result.value().nodes[0].light->intensity == 4.0f);
  CHECK(result.value().nodes[0].light->range == 12.5f);
}

TEST_CASE("decodeSceneArtifact rejects a scene declaring a second directional light (TooManyLights)",
          "[asset_system][scene][light]") {
  ValidatedSceneNode node0;
  node0.light = DecodedLight{DecodedLightKind::Directional, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
  ValidatedSceneNode node1;
  node1.light = DecodedLight{DecodedLightKind::Directional, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};

  const auto bytes = encodeSceneArtifact({node0, node1}, {std::nullopt, std::nullopt}, std::nullopt);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::TooManyLights);
}

TEST_CASE("decodeSceneArtifact rejects a scene declaring a fifth point light (TooManyLights)",
          "[asset_system][scene][light]") {
  std::vector<ValidatedSceneNode> nodes;
  std::vector<std::optional<std::size_t>> parents;
  for (int i = 0; i < 5; ++i) {
    ValidatedSceneNode node;
    node.light = DecodedLight{DecodedLightKind::Point, 1.0f, 1.0f, 1.0f, 1.0f, 5.0f};
    nodes.push_back(node);
    parents.push_back(std::nullopt);
  }
  const auto bytes = encodeSceneArtifact(nodes, parents, std::nullopt);
  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::TooManyLights);
}

TEST_CASE("decodeSceneArtifact independently re-validates a light's own out-of-range color, never trusting the "
          "cooker",
          "[asset_system][scene][light]") {
  // A real, hand-corrupted artifact byte buffer -- never merely
  // re-running the parse-time NonUnitNormal-shaped case.
  ValidatedSceneNode node;
  node.light = DecodedLight{DecodedLightKind::Directional, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f};
  auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  // color_r at record offset 84, absolute offset 24 + 84 = 108. Corrupt
  // to 2.0f (out of [0, 1]).
  const auto colorRBytes = std::bit_cast<std::array<std::byte, 4>>(2.0f);
  for (std::size_t i = 0; i < 4; ++i) bytes[108 + i] = colorRBytes[i];

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::NonFiniteValue);
}

TEST_CASE("decodeSceneArtifact independently re-validates a light's own negative intensity, never trusting the "
          "cooker",
          "[asset_system][scene][light]") {
  ValidatedSceneNode node;
  node.light = DecodedLight{DecodedLightKind::Directional, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f};
  auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  // intensity at record offset 96, absolute offset 24 + 96 = 120.
  const auto intensityBytes = std::bit_cast<std::array<std::byte, 4>>(-1.0f);
  for (std::size_t i = 0; i < 4; ++i) bytes[120 + i] = intensityBytes[i];

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::NonFiniteValue);
}

TEST_CASE("decodeSceneArtifact independently re-validates a point light's own non-positive range, never trusting "
          "the cooker",
          "[asset_system][scene][light]") {
  ValidatedSceneNode node;
  node.light = DecodedLight{DecodedLightKind::Point, 0.5f, 0.5f, 0.5f, 1.0f, 5.0f};
  auto bytes = encodeSceneArtifact({node}, {std::nullopt}, std::nullopt);
  // range at record offset 100, absolute offset 24 + 100 = 124.
  const auto rangeBytes = std::bit_cast<std::array<std::byte, 4>>(0.0f);
  for (std::size_t i = 0; i < 4; ++i) bytes[124 + i] = rangeBytes[i];

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::NonFiniteValue);
}

TEST_CASE("decodeSceneArtifact rejects an out-of-range index for the moved parent slot (offset 104/108)",
          "[asset_system][scene][light]") {
  // Confirms the moved parent-slot offset (Plan 0019 P4: 76/80 -> 104/108)
  // is the real, current one this decoder actually reads -- not the
  // pre-Plan-0019 offset.
  ValidatedSceneNode node0;
  ValidatedSceneNode node1;
  auto bytes = encodeSceneArtifact({node0, node1}, {std::nullopt, std::size_t{0}}, std::nullopt);
  // node1's own record starts at 24 + 112 = 136; its own has_parent/
  // parent_index are at relative 104/108, absolute 240/244.
  const auto outOfRangeIndex = std::bit_cast<std::array<std::byte, 4>>(std::uint32_t{99});
  for (std::size_t i = 0; i < 4; ++i) bytes[244 + i] = outOfRangeIndex[i];

  const auto result = decodeSceneArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneArtifactDecodeError::OutOfRangeParentIndex);
}

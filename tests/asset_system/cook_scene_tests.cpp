#include <atlantis/asset_system/cook_scene.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

// Matches cook_command_tests.cpp's own established TempDirGuard
// precedent exactly.
struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_cook_scene_tests" /
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

[[nodiscard]] std::vector<char> readFileBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string content = buffer.str();
  return std::vector<char>(content.begin(), content.end());
}

[[nodiscard]] std::vector<fs::path> findTempFiles(const fs::path& dir, const std::string& stem) {
  std::vector<fs::path> found;
  if (!fs::exists(dir)) return found;
  for (const auto& entry : fs::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    if (name.find(stem) == 0 && name.find(".tmp-") != std::string::npos) found.push_back(entry.path());
  }
  return found;
}

constexpr std::string_view kValidThreeNodeSource =
    "atlantis_scene_source_version: 1\n"
    "node_count: 3\n"
    "active_camera: 3\n"
    "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "mesh=meshes/minimal_cube.mesh.txt\n"
    "node: node_id=2 parent=1 position=1.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "mesh=meshes/minimal_cube.mesh.txt\n"
    "node: node_id=3 parent=none position=0.0 2.0 5.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "camera_fov_y=1.0 camera_near_z=0.1 camera_far_z=100.0\n";

}  // namespace

TEST_CASE("cookScene succeeds on a well-formed scene and writes both output files", "[asset_system][scene]") {
  TempDirGuard dir("positive");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  const fs::path metadataPath = dir.path / "scene.ascene.meta.txt";
  writeFile(sourcePath, std::string(kValidThreeNodeSource));

  const auto result = cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());
  CHECK(fs::exists(artifactPath));
  CHECK(fs::exists(metadataPath));
}

TEST_CASE("cookScene rejects an unreadable source file", "[asset_system][scene]") {
  TempDirGuard dir("unreadable");
  const auto result = cookScene((dir.path / "does_not_exist.scene.txt").string(), (dir.path / "out.ascene").string(),
                                 (dir.path / "out.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::SourceFileUnreadable);
  CHECK_FALSE(fs::exists(dir.path / "out.ascene"));
}

TEST_CASE("cookScene V28: rejects an empty scene (node_count: 0), writing no artifact", "[asset_system][scene]") {
  TempDirGuard dir("empty_scene");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 0\n"
            "active_camera: none\n");

  const auto result = cookScene(sourcePath.string(), artifactPath.string(), (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::EmptyScene);
  CHECK_FALSE(fs::exists(artifactPath));
}

TEST_CASE("cookScene V2: rejects a duplicate node_id, writing no artifact", "[asset_system][scene]") {
  TempDirGuard dir("duplicate_id");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 2\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
            "node: node_id=1 parent=none position=1.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), artifactPath.string(), (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::DuplicateNodeId);
  CHECK_FALSE(fs::exists(artifactPath));
}

TEST_CASE("cookScene V3: rejects a parent naming an undeclared node_id", "[asset_system][scene]") {
  TempDirGuard dir("undeclared_parent");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=99 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::UndeclaredParentReference);
}

TEST_CASE("cookScene V4: rejects a direct self-parent cycle", "[asset_system][scene]") {
  TempDirGuard dir("self_cycle");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=1 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::ParentCycle);
}

TEST_CASE("cookScene V4: rejects a multi-hop (4-node) parent cycle", "[asset_system][scene]") {
  TempDirGuard dir("multi_hop_cycle");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 4\n"
            "active_camera: none\n"
            "node: node_id=1 parent=4 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
            "node: node_id=2 parent=1 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
            "node: node_id=3 parent=2 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
            "node: node_id=4 parent=3 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::ParentCycle);
}

TEST_CASE("cookScene V5: rejects active_camera naming an undeclared node_id", "[asset_system][scene]") {
  TempDirGuard dir("undeclared_camera");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 1\n"
            "active_camera: 99\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::UndeclaredActiveCameraReference);
}

TEST_CASE("cookScene V6: rejects active_camera naming a node with no camera_* fields", "[asset_system][scene]") {
  TempDirGuard dir("camera_missing_camera");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 1\n"
            "active_camera: 1\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::ActiveCameraMissingCamera);
}

TEST_CASE("cookScene V7: rejects a non-finite authored float", "[asset_system][scene]") {
  TempDirGuard dir("non_finite");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=nan 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::NonFiniteValue);
}

TEST_CASE("cookScene V7: rejects a non-finite camera field", "[asset_system][scene]") {
  TempDirGuard dir("non_finite_camera");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 1\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "camera_fov_y=inf camera_near_z=0.1 camera_far_z=100.0\n");

  const auto result = cookScene(sourcePath.string(), (dir.path / "scene.ascene").string(),
                                 (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::NonFiniteValue);
}

TEST_CASE("cookScene V12: cooking the same source twice produces byte-identical output", "[asset_system][scene]") {
  TempDirGuard dir("determinism");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  writeFile(sourcePath, std::string(kValidThreeNodeSource));

  const fs::path artifactA = dir.path / "a.ascene";
  const fs::path metadataA = dir.path / "a.ascene.meta.txt";
  const fs::path artifactB = dir.path / "b.ascene";
  const fs::path metadataB = dir.path / "b.ascene.meta.txt";

  REQUIRE(cookScene(sourcePath.string(), artifactA.string(), metadataA.string()).isOk());
  REQUIRE(cookScene(sourcePath.string(), artifactB.string(), metadataB.string()).isOk());

  CHECK(readFileBytes(artifactA) == readFileBytes(artifactB));
  CHECK(readFileBytes(metadataA) == readFileBytes(metadataB));
}

TEST_CASE("cookScene V12: a validation failure on re-cook does not disturb prior valid output",
          "[asset_system][scene]") {
  TempDirGuard dir("no_disturb");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  const fs::path metadataPath = dir.path / "scene.ascene.meta.txt";
  writeFile(sourcePath, std::string(kValidThreeNodeSource));

  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());
  const std::vector<char> validArtifact = readFileBytes(artifactPath);
  const std::vector<char> validMetadata = readFileBytes(metadataPath);

  writeFile(sourcePath, "not a valid scene source\n");
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isErr());

  CHECK(readFileBytes(artifactPath) == validArtifact);
  CHECK(readFileBytes(metadataPath) == validMetadata);
  CHECK(findTempFiles(dir.path, "scene.ascene").empty());
}

TEST_CASE("cookScene V12: reports a genuine rename failure cleanly, with no leftover temp file",
          "[asset_system][scene]") {
  // Neither POSIX nor Win32 permits renaming a regular file onto an
  // existing directory -- occupying the artifact's own output path
  // with a directory forces writeBytesAtomically()'s own failure
  // branch, matching cook_command_tests.cpp's own established
  // technique exactly.
  TempDirGuard dir("rename_failure");
  const fs::path sourcePath = dir.path / "scene.scene.txt";
  const fs::path artifactPath = dir.path / "scene.ascene";
  writeFile(sourcePath, std::string(kValidThreeNodeSource));
  fs::create_directories(artifactPath);

  const auto result = cookScene(sourcePath.string(), artifactPath.string(), (dir.path / "scene.ascene.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneCookError::ArtifactWriteFailed);
  CHECK(fs::is_directory(artifactPath));
  CHECK(findTempFiles(dir.path, "scene.ascene").empty());
}

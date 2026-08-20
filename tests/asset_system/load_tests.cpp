#include <atlantis/asset_system/load.h>

#include <atlantis/asset_system/cook.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_system_load_tests" /
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

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 1\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0\n"
    "index: 0 1 2\n";

// Cooks a real, valid triangle asset into dir, returning
// {artifactPath, metadataPath}.
[[nodiscard]] std::pair<std::string, std::string> cookValidTriangle(const fs::path& dir) {
  const fs::path sourcePath = dir / "triangle.mesh.txt";
  {
    std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
    source << kValidTriangleSource;
  }
  const fs::path artifactPath = dir / "triangle.amesh";
  const fs::path metadataPath = dir / "triangle.amesh.meta.txt";
  const auto result =
      cookStaticMesh(sourcePath.string(), "triangle.mesh.txt", artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());
  return {artifactPath.string(), metadataPath.string()};
}

void writeFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

}  // namespace

TEST_CASE("loadStaticMeshAsset loads a well-formed artifact/metadata pair", "[asset_system]") {
  TempDirGuard dir("success");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().vertexCount() == 3);
  CHECK(result.value().indexCount() == 3);
  CHECK(result.value().vertexStrideBytes() == 24);
}

TEST_CASE("loadStaticMeshAsset fails when the artifact file does not exist", "[asset_system]") {
  TempDirGuard dir("missing_artifact");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset((dir.path / "does_not_exist.amesh").string(), metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::ArtifactFileUnreadable);
}

TEST_CASE("loadStaticMeshAsset fails when the metadata file does not exist", "[asset_system]") {
  TempDirGuard dir("missing_metadata");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset(artifactPath, (dir.path / "does_not_exist.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataFileUnreadable);
}

TEST_CASE("loadStaticMeshAsset fails when the artifact fails to decode", "[asset_system]") {
  TempDirGuard dir("bad_artifact");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  writeFile(artifactPath, "not a valid artifact");

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::ArtifactDecodeFailed);
}

TEST_CASE("loadStaticMeshAsset fails when the metadata fails to parse", "[asset_system]") {
  TempDirGuard dir("bad_metadata");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  writeFile(metadataPath, "not valid metadata\n");

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataParseFailed);
}

TEST_CASE("loadStaticMeshAsset detects a deliberate artifact/metadata mismatch", "[asset_system]") {
  TempDirGuard dir("mismatch");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  // Cook a second, different asset and swap in its metadata -- same
  // valid format on both sides, but the recorded fields (asset_id,
  // vertex_count) now disagree with the artifact's own header.
  const fs::path otherSourcePath = dir.path / "other.mesh.txt";
  writeFile(otherSourcePath,
            "atlantis_static_mesh_source_version: 1\n"
            "vertex_count: 4\n"
            "index_count: 6\n"
            "vertex: 0.0 0.0 0.0 0.0 0.0 0.0\n"
            "vertex: 1.0 0.0 0.0 0.0 0.0 0.0\n"
            "vertex: 1.0 1.0 0.0 0.0 0.0 0.0\n"
            "vertex: 0.0 1.0 0.0 0.0 0.0 0.0\n"
            "index: 0 1 2\n"
            "index: 2 3 0\n");
  const fs::path otherArtifactPath = dir.path / "other.amesh";
  const fs::path otherMetadataPath = dir.path / "other.amesh.meta.txt";
  const auto otherResult = cookStaticMesh(otherSourcePath.string(), "other.mesh.txt", otherArtifactPath.string(),
                                           otherMetadataPath.string());
  REQUIRE(otherResult.isOk());

  fs::copy_file(otherMetadataPath, metadataPath, fs::copy_options::overwrite_existing);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataArtifactMismatch);
}

TEST_CASE(
    "loadStaticMeshAsset detects a metadata file whose own recorded asset_id and source_logical_path disagree "
    "with each other, even when asset_id still matches the artifact",
    "[asset_system]") {
  TempDirGuard dir("self_inconsistent_metadata");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  // Individually well-formed and passes the artifact-vs-metadata check
  // above (asset_id/counts still agree with the artifact's own header)
  // -- but source_logical_path no longer hashes to that same asset_id,
  // an internal contradiction within the metadata file itself that the
  // artifact-vs-metadata check alone cannot see.
  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  const std::string oldLine = "source_logical_path: triangle.mesh.txt";
  const std::string newLine = "source_logical_path: some/other/path.mesh.txt";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), newLine);
  writeFile(metadataPath, metadataText);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataArtifactMismatch);
}

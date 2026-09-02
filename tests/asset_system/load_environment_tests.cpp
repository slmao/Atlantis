#include <atlantis/asset_system/load_environment.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/environment_artifact.h>
#include <atlantis/asset_system/environment_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;
std::atomic<int> gCounter{0};

struct TempDir {
  fs::path path = fs::temp_directory_path() / "atlantis_load_environment_tests" /
                  std::to_string(gCounter.fetch_add(1));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

[[nodiscard]] EnvironmentAssetData makeData() {
  EnvironmentAssetData data;
  data.faceSize = 1;
  data.mipCount = 1;
  data.dfgWidth = 1;
  data.dfgHeight = 1;
  data.specularRgba16Float.resize(6 * 4, 0x3C00U);
  data.dfgRg16Float.resize(2, 0x3800U);
  return data;
}

void writeBytes(const fs::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeText(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

struct Pair {
  fs::path artifact;
  fs::path metadata;
};

[[nodiscard]] Pair writeValidPair(const fs::path& directory) {
  const std::string logicalPath = "environments/studio.hdr";
  const AssetId assetId = computeAssetId(logicalPath);
  const EnvironmentAssetData data = makeData();
  Pair pair{directory / "studio.aenv", directory / "studio.aenv.meta.txt"};
  writeBytes(pair.artifact, encodeEnvironmentArtifact(assetId, data));
  EnvironmentMetadata metadata{assetId, logicalPath, data.faceSize, data.mipCount, data.dfgWidth, data.dfgHeight};
  writeText(pair.metadata, serializeEnvironmentMetadata(metadata));
  return pair;
}

}  // namespace

TEST_CASE("loadEnvironmentAsset validates and returns CPU-only data", "[asset_system]") {
  TempDir directory;
  const Pair pair = writeValidPair(directory.path);
  const auto loaded = loadEnvironmentAsset(pair.artifact, pair.metadata);
  REQUIRE(loaded.isOk());
  CHECK(loaded.value().faceSize == 1);
  CHECK(loaded.value().specularRgba16Float.size() == 24);
  CHECK(loaded.value().dfgRg16Float.size() == 2);
}

TEST_CASE("loadEnvironmentAsset reports I/O and parse domains", "[asset_system]") {
  TempDir directory;
  const Pair pair = writeValidPair(directory.path);
  CHECK(loadEnvironmentAsset(directory.path / "missing.aenv", pair.metadata).error() ==
        EnvironmentLoadError::ArtifactFileUnreadable);
  CHECK(loadEnvironmentAsset(pair.artifact, directory.path / "missing.meta").error() ==
        EnvironmentLoadError::MetadataFileUnreadable);
  writeText(pair.metadata, "invalid\n");
  CHECK(loadEnvironmentAsset(pair.artifact, pair.metadata).error() == EnvironmentLoadError::MetadataParseFailed);
}

TEST_CASE("loadEnvironmentAsset enforces embedded metadata and path-derived identity agreement", "[asset_system]") {
  TempDir directory;
  const Pair pair = writeValidPair(directory.path);
  EnvironmentMetadata metadata;
  metadata.assetId = computeAssetId("environments/other.hdr");
  metadata.sourceLogicalPath = "environments/studio.hdr";
  metadata.faceSize = 1;
  metadata.mipCount = 1;
  metadata.dfgWidth = 1;
  metadata.dfgHeight = 1;
  writeText(pair.metadata, serializeEnvironmentMetadata(metadata));
  CHECK(loadEnvironmentAsset(pair.artifact, pair.metadata).error() ==
        EnvironmentLoadError::MetadataArtifactMismatch);
}

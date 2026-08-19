#include <cook_command.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::runCookCommand;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

// RAII guard so a REQUIRE failure mid-test still cleans up via stack
// unwinding, matching this project's own established TempDirGuard
// precedent (tests/image_regression/*_tests.cpp).
struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_cooker_cmd_tests" /
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

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 1\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0\n"
    "index: 0 1 2\n";

}  // namespace

TEST_CASE("runCookCommand cooks a well-formed asset and writes a stamp", "[asset_cooker]") {
  TempDirGuard dir("cook_success");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path outputDir = dir.path / "out";
  writeFile(assetRoot / "meshes" / "triangle.mesh.txt", std::string(kValidTriangleSource));

  CookCommandRequest request;
  request.sourcePath = (assetRoot / "meshes" / "triangle.mesh.txt").string();
  request.assetRoot = assetRoot.string();
  request.outputDir = outputDir.string();
  request.stampPath = (outputDir / "triangle.stamp").string();

  CHECK(runCookCommand(request) == 0);
  CHECK(fs::exists(outputDir / "meshes" / "triangle.amesh"));
  CHECK(fs::exists(outputDir / "meshes" / "triangle.amesh.meta.txt"));
  CHECK(fs::exists(outputDir / "triangle.stamp"));
}

TEST_CASE("runCookCommand fails when the source file does not exist", "[asset_cooker]") {
  TempDirGuard dir("cook_missing_source");
  const fs::path assetRoot = dir.path / "assets";
  fs::create_directories(assetRoot);

  CookCommandRequest request;
  request.sourcePath = (assetRoot / "meshes" / "does_not_exist.mesh.txt").string();
  request.assetRoot = assetRoot.string();
  request.outputDir = (dir.path / "out").string();

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand fails on a malformed source", "[asset_cooker]") {
  TempDirGuard dir("cook_malformed");
  const fs::path assetRoot = dir.path / "assets";
  writeFile(assetRoot / "meshes" / "bad.mesh.txt", "not a valid mesh source\n");

  CookCommandRequest request;
  request.sourcePath = (assetRoot / "meshes" / "bad.mesh.txt").string();
  request.assetRoot = assetRoot.string();
  request.outputDir = (dir.path / "out").string();

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand fails when the source path escapes the asset root", "[asset_cooker]") {
  TempDirGuard dir("cook_escaping_path");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path outsideRoot = dir.path / "outside.mesh.txt";
  writeFile(outsideRoot, std::string(kValidTriangleSource));
  fs::create_directories(assetRoot);

  CookCommandRequest request;
  request.sourcePath = outsideRoot.string();
  request.assetRoot = assetRoot.string();
  request.outputDir = (dir.path / "out").string();

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand's validate-set mode accepts a valid declared set", "[asset_cooker]") {
  TempDirGuard dir("validate_set_ok");
  const fs::path listPath = dir.path / "declared_assets.txt";
  writeFile(listPath, "meshes/a.mesh.txt\nmeshes/b.mesh.txt\n");

  CookCommandRequest request;
  request.isValidateSet = true;
  request.assetListPath = listPath.string();

  CHECK(runCookCommand(request) == 0);
}

TEST_CASE("runCookCommand's validate-set mode rejects an exact duplicate logical path", "[asset_cooker]") {
  TempDirGuard dir("validate_set_duplicate");
  const fs::path listPath = dir.path / "declared_assets.txt";
  writeFile(listPath, "meshes/a.mesh.txt\nmeshes/a.mesh.txt\n");

  CookCommandRequest request;
  request.isValidateSet = true;
  request.assetListPath = listPath.string();

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand's validate-set mode rejects a case-only-differing pair", "[asset_cooker]") {
  TempDirGuard dir("validate_set_case_conflict");
  const fs::path listPath = dir.path / "declared_assets.txt";
  writeFile(listPath, "meshes/Cube.mesh.txt\nmeshes/cube.mesh.txt\n");

  CookCommandRequest request;
  request.isValidateSet = true;
  request.assetListPath = listPath.string();

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand's validate-set mode rejects a declared path that fails normalization", "[asset_cooker]") {
  TempDirGuard dir("validate_set_invalid_path");
  const fs::path listPath = dir.path / "declared_assets.txt";
  writeFile(listPath, "../escapes.mesh.txt\n");

  CookCommandRequest request;
  request.isValidateSet = true;
  request.assetListPath = listPath.string();

  CHECK(runCookCommand(request) != 0);
}

TEST_CASE("runCookCommand's validate-set mode fails when the asset list file is missing", "[asset_cooker]") {
  TempDirGuard dir("validate_set_missing_list");

  CookCommandRequest request;
  request.isValidateSet = true;
  request.assetListPath = (dir.path / "does_not_exist.txt").string();

  CHECK(runCookCommand(request) != 0);
}

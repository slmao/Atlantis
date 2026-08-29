#include <cook_command.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

[[nodiscard]] std::string readFileText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Every entry whose name starts with the given stem and contains
// ".tmp-" -- writeBytesAtomically()'s own temp-file naming convention
// (Plan 0012 Section D10). Used to prove no temp file survives a
// failed cook.
[[nodiscard]] std::vector<fs::path> findTempFiles(const fs::path& dir, const std::string& stem) {
  std::vector<fs::path> found;
  if (!fs::exists(dir)) return found;
  for (const auto& entry : fs::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    if (name.find(stem) == 0 && name.find(".tmp-") != std::string::npos) found.push_back(entry.path());
  }
  return found;
}

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

// A different, still-valid mesh, distinct from kValidTriangleSource --
// used to prove an overwrite of an existing destination actually
// replaces its content, not merely leaves the old content in place
// while silently reporting success.
constexpr std::string_view kValidSquareSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 4\n"
    "index_count: 6\n"
    "vertex: 0.0 0.0 0.0 1.0 1.0 1.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 1.0 1.0 1.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 1.0 0.0 1.0 1.0 1.0 1.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 1.0 1.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n"
    "index: 2 3 0\n";

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

TEST_CASE("runCookCommand fails on a well-formed but old (pre-UV0), version-1 source", "[asset_cooker]") {
  // Plan 0017 Section D4/V11: version 1 (six-field, no UV0) is rejected
  // outright through the real CLI, no compatible/dual-version read --
  // distinct from "malformed source" above, since this input is
  // otherwise well-formed under the old grammar.
  TempDirGuard dir("cook_old_version");
  const fs::path assetRoot = dir.path / "assets";
  writeFile(assetRoot / "meshes" / "old.mesh.txt",
            "atlantis_static_mesh_source_version: 1\n"
            "vertex_count: 3\n"
            "index_count: 3\n"
            "vertex: 0.0 0.0 0.0 1.0 0.0 0.0\n"
            "vertex: 1.0 0.0 0.0 0.0 1.0 0.0\n"
            "vertex: 0.0 1.0 0.0 0.0 0.0 1.0\n"
            "index: 0 1 2\n");

  CookCommandRequest request;
  request.sourcePath = (assetRoot / "meshes" / "old.mesh.txt").string();
  request.assetRoot = assetRoot.string();
  request.outputDir = (dir.path / "out").string();

  CHECK(runCookCommand(request) != 0);
  CHECK_FALSE(fs::exists(dir.path / "out" / "old.amesh"));
}

TEST_CASE("runCookCommand fails on a well-formed but old (pre-normal), version-2 source", "[asset_cooker]") {
  // Plan 0020: version 2 (eight-field, no normal) is now also rejected
  // outright through the real CLI, exactly like version 1 already was
  // -- the direct successor to the version-1 case above, now that
  // version 2 is also superseded.
  TempDirGuard dir("cook_old_v2_version");
  const fs::path assetRoot = dir.path / "assets";
  writeFile(assetRoot / "meshes" / "old_v2.mesh.txt",
            "atlantis_static_mesh_source_version: 2\n"
            "vertex_count: 3\n"
            "index_count: 3\n"
            "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
            "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
            "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n"
            "index: 0 1 2\n");

  CookCommandRequest request;
  request.sourcePath = (assetRoot / "meshes" / "old_v2.mesh.txt").string();
  request.assetRoot = assetRoot.string();
  request.outputDir = (dir.path / "out").string();

  CHECK(runCookCommand(request) != 0);
  CHECK_FALSE(fs::exists(dir.path / "out" / "old_v2.amesh"));
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

TEST_CASE("runCookCommand cooking into an existing valid destination successfully overwrites it",
          "[asset_cooker]") {
  // Plan 0012 Section D10 / Verification V11: the "destination already
  // exists" case, exercised through the real cookStaticMesh() code
  // path -- not only a raw std::filesystem::rename() probe.
  TempDirGuard dir("cook_overwrite");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path outputDir = dir.path / "out";
  const fs::path sourcePath = assetRoot / "mesh.mesh.txt";
  const fs::path artifactPath = outputDir / "mesh.amesh";
  const fs::path metadataPath = outputDir / "mesh.amesh.meta.txt";

  writeFile(sourcePath, std::string(kValidTriangleSource));
  CookCommandRequest request;
  request.sourcePath = sourcePath.string();
  request.assetRoot = assetRoot.string();
  request.outputDir = outputDir.string();
  REQUIRE(runCookCommand(request) == 0);
  const std::string firstArtifact = readFileText(artifactPath);
  const std::string firstMetadata = readFileText(metadataPath);

  // Cook a different, still-valid mesh into the exact same destination.
  writeFile(sourcePath, std::string(kValidSquareSource));
  REQUIRE(runCookCommand(request) == 0);
  const std::string secondArtifact = readFileText(artifactPath);
  const std::string secondMetadata = readFileText(metadataPath);

  CHECK(secondArtifact != firstArtifact);
  CHECK(secondMetadata != firstMetadata);
  CHECK(findTempFiles(outputDir, "mesh.amesh").empty());
}

TEST_CASE("runCookCommand's failed cook leaves no temp file and no partial output", "[asset_cooker]") {
  TempDirGuard dir("cook_failure_no_partial");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path outputDir = dir.path / "out";
  const fs::path sourcePath = assetRoot / "mesh.mesh.txt";
  writeFile(sourcePath, "not a valid mesh source\n");

  CookCommandRequest request;
  request.sourcePath = sourcePath.string();
  request.assetRoot = assetRoot.string();
  request.outputDir = outputDir.string();

  REQUIRE(runCookCommand(request) != 0);
  CHECK_FALSE(fs::exists(outputDir / "mesh.amesh"));
  CHECK_FALSE(fs::exists(outputDir / "mesh.amesh.meta.txt"));
  CHECK(findTempFiles(outputDir, "mesh.amesh").empty());
}

TEST_CASE("runCookCommand's failed cook after a prior success leaves the existing valid output byte-unchanged",
          "[asset_cooker]") {
  // Plan 0012 Section D10 / Verification V11: a failed re-cook must
  // never downgrade a previously-valid artifact to a partial or
  // corrupted one.
  TempDirGuard dir("cook_failure_preserves_existing");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path outputDir = dir.path / "out";
  const fs::path sourcePath = assetRoot / "mesh.mesh.txt";
  const fs::path artifactPath = outputDir / "mesh.amesh";
  const fs::path metadataPath = outputDir / "mesh.amesh.meta.txt";

  writeFile(sourcePath, std::string(kValidTriangleSource));
  CookCommandRequest request;
  request.sourcePath = sourcePath.string();
  request.assetRoot = assetRoot.string();
  request.outputDir = outputDir.string();
  REQUIRE(runCookCommand(request) == 0);
  const std::string validArtifact = readFileText(artifactPath);
  const std::string validMetadata = readFileText(metadataPath);

  // Corrupt the source in place and re-cook into the same destination.
  writeFile(sourcePath, "not a valid mesh source\n");
  REQUIRE(runCookCommand(request) != 0);

  CHECK(readFileText(artifactPath) == validArtifact);
  CHECK(readFileText(metadataPath) == validMetadata);
  CHECK(findTempFiles(outputDir, "mesh.amesh").empty());
}

TEST_CASE("runCookCommand reports a genuine rename failure cleanly, with no leftover temp file", "[asset_cooker]") {
  // Forces an actual std::filesystem::rename() failure -- neither
  // POSIX nor Win32 permits renaming a regular file onto an existing
  // directory -- to exercise the failure branch of
  // writeBytesAtomically(), not only its happy path.
  TempDirGuard dir("cook_rename_failure");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path outputDir = dir.path / "out";
  const fs::path sourcePath = assetRoot / "mesh.mesh.txt";
  writeFile(sourcePath, std::string(kValidTriangleSource));
  fs::create_directories(outputDir / "mesh.amesh");  // occupies the artifact's own output path as a directory

  CookCommandRequest request;
  request.sourcePath = sourcePath.string();
  request.assetRoot = assetRoot.string();
  request.outputDir = outputDir.string();

  REQUIRE(runCookCommand(request) != 0);
  CHECK(fs::is_directory(outputDir / "mesh.amesh"));  // left untouched, not replaced with a partial file
  CHECK(findTempFiles(outputDir, "mesh.amesh").empty());
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

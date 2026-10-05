#include "test_catalog_source.h"
#include "test_cooked_build.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Plan 0012 Section D7 (V5): runs the REAL atlantis_asset_cooker
// executable twice against the same source, into two distinct output
// directories, and byte-compares both the .amesh artifact and the
// .amesh.meta.txt sidecar -- proving determinism end to end through the
// actual CLI, not merely through cookStaticMesh() called in-process.
// "tool"-labeled because it needs the real, just-built cooker
// executable at test-run time, matching
// tests/tools/shader_compiler/toolchain_integration_tests.cpp's own
// "tool" label precedent -- no GPU/Vulkan device is needed.

using atlantis::tools::asset_cooker::test::writeCatalogSourceCovering;

namespace {

namespace fs = std::filesystem;

// Per-process tag: catch_discover_tests runs each TEST_CASE in its own
// process under ctest -j, and the counter below restarts at 0 in each.
const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_cooker_determinism_tests" /
              (label + "_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

[[nodiscard]] std::string quoted(const std::string& s) {
  return "\"" + s + "\"";
}

// Runs the cooker with the given output directory and returns its exit
// code. The command string is wrapped in one additional, outermost pair
// of quotes before being handed to std::system(): on Windows,
// std::system() invokes "cmd.exe /c <string>", and when <string> itself
// begins with a quoted token (here, the quoted executable path), cmd.exe
// strips what it interprets as an enclosing quote pair from the whole
// string before parsing it -- corrupting the command unless an extra,
// genuinely-enclosing pair is present for it to strip instead. This is
// a well-documented Windows system()-specific quoting rule, not a
// project-specific process-launch mechanism (this test uses
// std::system() deliberately, for its own simplicity, rather than
// duplicating Shader System's private CreateProcessW wrapper).
[[nodiscard]] int runCooker(const std::string& cookerExecutable, const std::string& sourcePath,
                             const std::string& assetRoot, const std::string& outputDir,
                             const std::string& catalogSource) {
  const std::string command = quoted(cookerExecutable) + " --source=" + quoted(sourcePath) +
                               " --asset-root=" + quoted(assetRoot) + " --output-dir=" + quoted(outputDir) +
                               " --catalog-source=" + quoted(catalogSource);
  const std::string wrapped = "\"" + command + "\"";
  return std::system(wrapped.c_str());
}

[[nodiscard]] int runEnvironmentCooker(const std::string& cookerExecutable, const std::string& sourcePath,
                                        const std::string& assetRoot, const std::string& outputDir) {
  const fs::path stampPath = fs::path(outputDir) / "studio.stamp";
  // Plan 0047 P7: the committed catalog source under the real asset root.
  const std::string catalogSource = (fs::path(assetRoot) / "asset_catalog.txt").string();
  const std::string command = quoted(cookerExecutable) + " --kind=environment --source=" + quoted(sourcePath) +
                              " --asset-root=" + quoted(assetRoot) + " --output-dir=" + quoted(outputDir) +
                              " --stamp=" + quoted(stampPath.string()) + " --catalog-source=" + quoted(catalogSource);
  return std::system(("\"" + command + "\"").c_str());
}

[[nodiscard]] std::vector<char> readFileBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string content = buffer.str();
  return std::vector<char>(content.begin(), content.end());
}

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

}  // namespace

TEST_CASE("The real atlantis_asset_cooker executable produces byte-identical output across two runs",
          "[asset_cooker][tool]") {
  TempDirGuard dir("determinism");
  const fs::path assetRoot = dir.path / "assets";
  const fs::path sourcePath = assetRoot / "meshes" / "triangle.mesh.txt";
  fs::create_directories(sourcePath.parent_path());
  {
    std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
    source << kValidTriangleSource;
  }

  const fs::path outputDirA = dir.path / "out_a";
  const fs::path outputDirB = dir.path / "out_b";

  const std::string cookerExecutable = ATLANTIS_ASSET_COOKER_EXECUTABLE;
  const std::string catalogSource =
      writeCatalogSourceCovering(assetRoot, dir.path / "asset_catalog.txt").string();

  REQUIRE(runCooker(cookerExecutable, sourcePath.string(), assetRoot.string(), outputDirA.string(), catalogSource) ==
          0);
  REQUIRE(runCooker(cookerExecutable, sourcePath.string(), assetRoot.string(), outputDirB.string(), catalogSource) ==
          0);

  const fs::path artifactA = outputDirA / "meshes" / "triangle.amesh";
  const fs::path artifactB = outputDirB / "meshes" / "triangle.amesh";
  const fs::path metadataA = outputDirA / "meshes" / "triangle.amesh.meta.txt";
  const fs::path metadataB = outputDirB / "meshes" / "triangle.amesh.meta.txt";

  REQUIRE(fs::exists(artifactA));
  REQUIRE(fs::exists(artifactB));
  REQUIRE(fs::exists(metadataA));
  REQUIRE(fs::exists(metadataB));

  CHECK(readFileBytes(artifactA) == readFileBytes(artifactB));
  CHECK(readFileBytes(metadataA) == readFileBytes(metadataB));
}

TEST_CASE("The real environment cooker produces byte-identical artifact and metadata across two processes",
          "[asset_cooker][environment][tool]") {
  TempDirGuard dir("environment_determinism");
  const fs::path outputDirA = dir.path / "out_a";
  const fs::path outputDirB = dir.path / "out_b";
  REQUIRE(runEnvironmentCooker(ATLANTIS_ASSET_COOKER_EXECUTABLE, ATLANTIS_IBL_STUDIO_SOURCE_PATH,
                               ATLANTIS_ASSET_ROOT, outputDirA.string()) == 0);
  REQUIRE(runEnvironmentCooker(ATLANTIS_ASSET_COOKER_EXECUTABLE, ATLANTIS_IBL_STUDIO_SOURCE_PATH,
                               ATLANTIS_ASSET_ROOT, outputDirB.string()) == 0);
  REQUIRE(fs::exists(outputDirA / "studio.aenv"));
  REQUIRE(fs::exists(outputDirB / "studio.aenv"));
  CHECK(readFileBytes(outputDirA / "studio.aenv") == readFileBytes(outputDirB / "studio.aenv"));
  CHECK(readFileBytes(outputDirA / "studio.aenv.meta.txt") ==
        readFileBytes(outputDirB / "studio.aenv.meta.txt"));
}

// Plan 0047 M4 (Spec 0047 determinism, measured): the real cooker assembles
// the same fragments twice, into two catalogs (and closures) beside each
// other, and both runs write the same bytes.
TEST_CASE("The real atlantis_asset_cooker assembles byte-identical catalogs across two runs",
          "[asset_cooker][assemble_catalog][tool]") {
  TempDirGuard dir("assemble_determinism");
  const atlantis::tools::asset_cooker::test::CookedBuild build = atlantis::tools::asset_cooker::test::cookSmallBuild(
      dir.path, fs::path(ATLANTIS_ASSET_ROOT) / "textures" / "textured_quad_source_unorm.png");

  const auto assemble = [&build](const std::string& tag) {
    const std::string command =
        quoted(ATLANTIS_ASSET_COOKER_EXECUTABLE) + " --kind=assemble-catalog --catalog-source=" +
        quoted(build.catalogSource.string()) + " --declarations=" + quoted(build.declarations.string()) +
        " --fragment-list=" + quoted(build.fragmentList.string()) + " --out=" +
        quoted((build.outDir / ("catalog_" + tag + ".txt")).string()) + " --closure=" +
        atlantis::asset_system::toString(build.scene) + "=" +
        quoted((build.outDir / ("closure_" + tag + ".txt")).string());
    return std::system(("\"" + command + "\"").c_str());
  };
  REQUIRE(assemble("a") == 0);
  REQUIRE(assemble("b") == 0);

  const std::vector<char> catalogA = readFileBytes(build.outDir / "catalog_a.txt");
  CHECK(catalogA.size() > 100);
  CHECK(catalogA == readFileBytes(build.outDir / "catalog_b.txt"));
  CHECK(readFileBytes(build.outDir / "closure_a.txt") == readFileBytes(build.outDir / "closure_b.txt"));
}

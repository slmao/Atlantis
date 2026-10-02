// Plan 0047 M3 (P7; ADR-0098 D1): a per-asset cook takes its GUID from the
// catalog source on the command line, or from --guid= on a cook-manifest
// line, and fails by name when the catalog has no entry or the wrong type.

#include <cook_command.h>
#include "test_catalog_source.h"

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using atlantis::asset_system::assetKey;
using atlantis::asset_system::parseAssetMetadata;
using atlantis::asset_system::toString;
using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookArgumentSource;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::parseCookArguments;
using atlantis::tools::asset_cooker::runCookCommand;
using atlantis::tools::asset_cooker::test::testCatalogGuid;
using atlantis::tools::asset_cooker::test::writeCatalogSourceCovering;

namespace fs = std::filesystem;

namespace {

constexpr const char* kTriangle =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

// A scratch asset root with one triangle mesh, removed on destruction.
class Scratch {
 public:
  Scratch() : root_(fs::temp_directory_path() / "atlantis_catalog_cook_command_tests") {
    fs::remove_all(root_);
    fs::create_directories(root_ / "assets" / "meshes");
    std::ofstream(root_ / "assets" / "meshes" / "tri.mesh.txt", std::ios::binary) << kTriangle;
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;

  [[nodiscard]] fs::path path(const std::string& relative) const { return root_ / relative; }

  [[nodiscard]] CookCommandRequest meshRequest() const {
    CookCommandRequest request;
    request.kind = AssetKind::StaticMesh;
    request.sourcePath = path("assets/meshes/tri.mesh.txt").string();
    request.assetRoot = path("assets").string();
    request.outputDir = path("out").string();
    return request;
  }

  void writeCatalog(const std::string& text) const {
    std::ofstream(path("asset_catalog.txt"), std::ios::binary) << text;
  }

 private:
  fs::path root_;
};

struct Outcome {
  int exitCode = 0;
  std::string err;
};

[[nodiscard]] Outcome run(const CookCommandRequest& request) {
  std::ostringstream captured;
  std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());
  const int exitCode = runCookCommand(request);
  std::cerr.rdbuf(previous);
  return {exitCode, captured.str()};
}

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("A cook writes the GUID its catalog source records", "[asset_cooker][catalog]") {
  const Scratch scratch;
  CookCommandRequest request = scratch.meshRequest();
  request.catalogSourcePath =
      writeCatalogSourceCovering(scratch.path("assets"), scratch.path("asset_catalog.txt")).string();
  REQUIRE(run(request).exitCode == 0);

  const auto metadata = parseAssetMetadata(readText(scratch.path("out/meshes/tri.amesh.meta.txt")));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().assetGuid == testCatalogGuid("meshes/tri.mesh.txt"));
  CHECK(metadata.value().assetId == assetKey(testCatalogGuid("meshes/tri.mesh.txt")));
  CHECK(metadata.value().sourceLogicalPath == "meshes/tri.mesh.txt");
}

TEST_CASE("A cook whose source has no catalog entry fails as SourceNotInCatalog", "[asset_cooker][catalog]") {
  const Scratch scratch;
  scratch.writeCatalog("atlantis_asset_catalog_source_version: 1\nentry_count: 0\n");
  CookCommandRequest request = scratch.meshRequest();
  request.catalogSourcePath = scratch.path("asset_catalog.txt").string();
  const Outcome outcome = run(request);
  CHECK(outcome.exitCode != 0);
  CHECK(outcome.err.find("SourceNotInCatalog") != std::string::npos);
  CHECK_FALSE(fs::exists(scratch.path("out/meshes/tri.amesh")));
}

TEST_CASE("A cook whose catalog entry has another type fails as CatalogTypeMismatch", "[asset_cooker][catalog]") {
  const Scratch scratch;
  scratch.writeCatalog("atlantis_asset_catalog_source_version: 1\nentry_count: 1\nasset: guid=" +
                       toString(testCatalogGuid("meshes/tri.mesh.txt")) +
                       " type=texture root=assets path=meshes/tri.mesh.txt\n");
  CookCommandRequest request = scratch.meshRequest();
  request.catalogSourcePath = scratch.path("asset_catalog.txt").string();
  const Outcome outcome = run(request);
  CHECK(outcome.exitCode != 0);
  CHECK(outcome.err.find("CatalogTypeMismatch") != std::string::npos);
  CHECK_FALSE(fs::exists(scratch.path("out/meshes/tri.amesh")));
}

TEST_CASE("A cook fails on a missing or invalid catalog source", "[asset_cooker][catalog]") {
  const Scratch scratch;
  CookCommandRequest request = scratch.meshRequest();
  request.catalogSourcePath = scratch.path("missing_catalog.txt").string();
  CHECK(run(request).exitCode != 0);

  scratch.writeCatalog("atlantis_asset_catalog_source_version: 9\nentry_count: 0\n");
  request.catalogSourcePath = scratch.path("asset_catalog.txt").string();
  CHECK(run(request).exitCode != 0);
  CHECK_FALSE(fs::exists(scratch.path("out/meshes/tri.amesh")));
}

TEST_CASE("A cook-manifest-line cook writes the --guid it was given", "[asset_cooker][catalog]") {
  const Scratch scratch;
  CookCommandRequest request = scratch.meshRequest();
  request.guid = "0047aaaa-0000-4000-8000-000000000001";
  REQUIRE(run(request).exitCode == 0);
  const auto metadata = parseAssetMetadata(readText(scratch.path("out/meshes/tri.amesh.meta.txt")));
  REQUIRE(metadata.isOk());
  CHECK(toString(metadata.value().assetGuid) == request.guid);
}

TEST_CASE("A cook's GUID source depends on where its arguments came from", "[asset_cooker][catalog]") {
  const std::vector<std::string> base = {"--kind=mesh", "--source=a.mesh.txt", "--asset-root=r", "--output-dir=o"};
  const auto parses = [&base](std::vector<std::string> extra, CookArgumentSource source) {
    std::vector<std::string> args = base;
    args.insert(args.end(), extra.begin(), extra.end());
    CookCommandRequest request;
    std::ostringstream err;
    return parseCookArguments(args, request, err, source);
  };
  const std::string guid = "--guid=0047aaaa-0000-4000-8000-000000000001";

  CHECK(parses({"--catalog-source=c.txt"}, CookArgumentSource::CommandLine));
  CHECK_FALSE(parses({}, CookArgumentSource::CommandLine));
  CHECK_FALSE(parses({guid}, CookArgumentSource::CommandLine));
  CHECK_FALSE(parses({"--catalog-source=c.txt", guid}, CookArgumentSource::CommandLine));

  // Plan 0047 M4: a manifest line also names its record's source.
  CHECK(parses({guid, "--catalog-id=content:street/input.gltf#mesh/0/0"}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({"--catalog-source=c.txt"}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({"--catalog-source=c.txt", guid}, CookArgumentSource::CookManifestLine));
}

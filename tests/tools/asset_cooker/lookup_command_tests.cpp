// Plan 0047 M2: --kind=lookup prints the catalog-source entry for a GUID or
// a <root>:<path>, and fails by name otherwise.

#include <catch2/catch_test_macros.hpp>

#include "cook_command.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::parseCookArguments;
using atlantis::tools::asset_cooker::runCookCommand;

namespace {

constexpr const char* kMeshLine =
    "asset: guid=01234567-89ab-4def-8123-456789abcdef type=mesh root=assets path=meshes/minimal_cube.mesh.txt";
constexpr const char* kImportLine =
    "asset: guid=11234567-89ab-4def-8123-456789abcdef type=gltf_import root=content path=bistro/bistro.gltf";

class TempCatalogSource {
 public:
  explicit TempCatalogSource(const std::string& text)
      : path_(std::filesystem::temp_directory_path() / "atlantis_lookup_command_test_catalog.txt") {
    std::ofstream(path_, std::ios::binary) << text;
  }
  ~TempCatalogSource() { std::filesystem::remove(path_); }
  TempCatalogSource(const TempCatalogSource&) = delete;
  TempCatalogSource& operator=(const TempCatalogSource&) = delete;

  [[nodiscard]] std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string validSource() {
  return std::string("atlantis_asset_catalog_source_version: 1\nentry_count: 2\n") + kMeshLine + "\n" + kImportLine +
         "\n";
}

struct LookupOutcome {
  int exitCode = 0;
  std::string out;
};

[[nodiscard]] LookupOutcome runLookup(const std::vector<std::string>& args) {
  CookCommandRequest request;
  std::ostringstream err;
  REQUIRE(parseCookArguments(args, request, err));
  std::ostringstream captured;
  std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
  const int exitCode = runCookCommand(request);
  std::cout.rdbuf(previous);
  return {exitCode, captured.str()};
}

}  // namespace

TEST_CASE("Lookup by GUID prints the entry", "[lookup]") {
  const TempCatalogSource catalog(validSource());
  const LookupOutcome outcome = runLookup(
      {"--kind=lookup", "--catalog-source=" + catalog.path(), "--guid=11234567-89ab-4def-8123-456789abcdef"});
  CHECK(outcome.exitCode == 0);
  CHECK(outcome.out == std::string(kImportLine) + "\n");
}

TEST_CASE("Lookup by root and path prints the entry", "[lookup]") {
  const TempCatalogSource catalog(validSource());
  const LookupOutcome outcome =
      runLookup({"--kind=lookup", "--catalog-source=" + catalog.path(), "--source=assets:meshes/minimal_cube.mesh.txt"});
  CHECK(outcome.exitCode == 0);
  CHECK(outcome.out == std::string(kMeshLine) + "\n");
}

TEST_CASE("Lookup fails for a miss, a bad key, or an invalid catalog source", "[lookup]") {
  const TempCatalogSource catalog(validSource());
  const std::string catalogFlag = "--catalog-source=" + catalog.path();
  CHECK(runLookup({"--kind=lookup", catalogFlag, "--guid=21234567-89ab-4def-8123-456789abcdef"}).exitCode != 0);
  CHECK(runLookup({"--kind=lookup", catalogFlag, "--source=content:meshes/minimal_cube.mesh.txt"}).exitCode != 0);
  CHECK(runLookup({"--kind=lookup", catalogFlag, "--source=meshes/minimal_cube.mesh.txt"}).exitCode != 0);
  CHECK(runLookup({"--kind=lookup", catalogFlag, "--guid=21234567-89AB-4def-8123-456789abcdef"}).exitCode != 0);

  const TempCatalogSource unsorted(std::string("atlantis_asset_catalog_source_version: 1\nentry_count: 2\n") +
                                   kImportLine + "\n" + kMeshLine + "\n");
  CHECK(runLookup({"--kind=lookup", "--catalog-source=" + unsorted.path(),
                   "--guid=01234567-89ab-4def-8123-456789abcdef"})
            .exitCode != 0);

  CHECK(runLookup({"--kind=lookup", "--catalog-source=" + catalog.path() + ".missing",
                   "--guid=01234567-89ab-4def-8123-456789abcdef"})
            .exitCode != 0);
}

TEST_CASE("Lookup needs a catalog source and exactly one key", "[lookup]") {
  const std::string guidFlag = "--guid=01234567-89ab-4def-8123-456789abcdef";
  const std::string sourceFlag = "--source=assets:meshes/minimal_cube.mesh.txt";
  for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
           {"--kind=lookup", guidFlag},
           {"--kind=lookup", "--catalog-source=c.txt"},
           {"--kind=lookup", "--catalog-source=c.txt", guidFlag, sourceFlag},
       }) {
    CookCommandRequest request;
    std::ostringstream err;
    CHECK_FALSE(parseCookArguments(args, request, err));
  }
}

TEST_CASE("The GUID flag is refused outside lookup", "[lookup]") {
  CookCommandRequest request;
  std::ostringstream err;
  CHECK_FALSE(parseCookArguments({"--kind=mesh", "--source=a.mesh.txt", "--asset-root=r", "--output-dir=o",
                                  "--guid=01234567-89ab-4def-8123-456789abcdef"},
                                 request, err));
  CHECK_FALSE(err.str().empty());
}

TEST_CASE("A cook manifest line cannot run a lookup", "[lookup]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atlantis_lookup_manifest_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const TempCatalogSource catalog(validSource());
  std::ofstream(dir / "asset_list.txt") << "";
  std::ofstream(dir / "cook_manifest.txt")
      << "--kind=lookup --catalog-source=" << catalog.path() << " --guid=01234567-89ab-4def-8123-456789abcdef\n";

  CookCommandRequest request;
  request.kind = AssetKind::CookManifest;
  request.importDir = dir.string();
  request.cookedDir = (dir / "cooked").string();
  request.contentParent = dir.string();
  request.manifestOutPath = (dir / "manifest.txt").string();
  CHECK(runCookCommand(request) != 0);

  std::filesystem::remove_all(dir);
}

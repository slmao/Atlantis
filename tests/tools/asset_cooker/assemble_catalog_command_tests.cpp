// Plan 0047 M4 (P13; ADR-0098 D2): --kind=assemble-catalog over a small
// real build, and --kind=lookup against the cooked catalog it writes.

#include <cook_command.h>
#include "test_cooked_build.h"

#include <atlantis/asset_system/asset_catalog.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace atlantis::asset_system;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::parseCookArguments;
using atlantis::tools::asset_cooker::runCookCommand;
using atlantis::tools::asset_cooker::test::CookedBuild;
using atlantis::tools::asset_cooker::test::cookSmallBuild;
using atlantis::tools::asset_cooker::test::testCatalogGuid;

namespace fs = std::filesystem;

namespace {

struct ScratchDir {
  fs::path path;
  explicit ScratchDir(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_assemble_catalog_command_tests" / label) {
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
};

[[nodiscard]] fs::path pngFixture() { return fs::path(ATLANTIS_ASSET_COOKER_TEST_FIXTURES_DIR) / "tiny_rgba.png"; }

struct Outcome {
  int exitCode = 1;
  std::string out;
  std::string err;
};

// Parses and runs one argument vector, capturing both streams.
[[nodiscard]] Outcome runArgs(const std::vector<std::string>& args) {
  Outcome outcome;
  std::ostringstream out;
  std::ostringstream err;
  std::streambuf* const previousOut = std::cout.rdbuf(out.rdbuf());
  std::streambuf* const previousErr = std::cerr.rdbuf(err.rdbuf());
  CookCommandRequest request;
  if (parseCookArguments(args, request, std::cerr)) outcome.exitCode = runCookCommand(request);
  std::cout.rdbuf(previousOut);
  std::cerr.rdbuf(previousErr);
  outcome.out = out.str();
  outcome.err = err.str();
  return outcome;
}

[[nodiscard]] std::vector<std::string> assembleArgs(const CookedBuild& build, const fs::path& out) {
  return {"--kind=assemble-catalog", "--catalog-source=" + build.catalogSource.string(),
          "--declarations=" + build.declarations.string(), "--fragment-list=" + build.fragmentList.string(),
          "--out=" + out.string()};
}

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("assemble-catalog writes the catalog and a closure from the build's fragments",
          "[asset_cooker][assemble_catalog]") {
  const ScratchDir dir("writes");
  const CookedBuild build = cookSmallBuild(dir.path, pngFixture());
  const fs::path out = build.outDir / "asset_catalog.txt";
  const fs::path closure = build.outDir / "s.catalog.txt";
  std::vector<std::string> args = assembleArgs(build, out);
  args.push_back("--closure=" + toString(build.scene) + "=" + closure.string());

  const Outcome outcome = runArgs(args);
  INFO(outcome.err);
  REQUIRE(outcome.exitCode == 0);
  CHECK(outcome.out.find("4 records") != std::string::npos);

  const auto records = parseAssetCatalogRecords(readText(out));
  REQUIRE(records.isOk());
  REQUIRE(records.value().size() == 4);
  for (const AssetCatalogRecord& record : records.value()) {
    INFO(formatAssetCatalogRecord(record));
    CHECK_FALSE(fs::path(record.artifact).is_absolute());
    CHECK(fs::exists(build.outDir / record.artifact));
    CHECK(fs::exists(build.outDir / record.metadata));
  }
  // The scene reaches every other asset, so its closure is the whole set.
  CHECK(readText(closure) == readText(out));
}

TEST_CASE("assemble-catalog writes nothing when assembly fails, and names the error",
          "[asset_cooker][assemble_catalog]") {
  const ScratchDir dir("fails");
  const CookedBuild build = cookSmallBuild(dir.path, pngFixture());
  fs::remove(build.outDir / "t.atex.catalog.txt");
  const fs::path out = build.outDir / "asset_catalog.txt";

  const Outcome outcome = runArgs(assembleArgs(build, out));
  CHECK(outcome.exitCode != 0);
  CHECK(outcome.err.find("FragmentUnreadable") != std::string::npos);
  CHECK_FALSE(fs::exists(out));
}

TEST_CASE("assemble-catalog requires its four inputs and a well-formed --closure", "[asset_cooker][assemble_catalog]") {
  const std::vector<std::string> full = {"--kind=assemble-catalog", "--catalog-source=c", "--declarations=d",
                                         "--fragment-list=f", "--out=o"};
  const auto parses = [](const std::vector<std::string>& args) {
    CookCommandRequest request;
    std::ostringstream err;
    return parseCookArguments(args, request, err);
  };
  CHECK(parses(full));
  for (std::size_t drop = 1; drop < full.size(); ++drop) {
    std::vector<std::string> missing = full;
    missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(drop));
    INFO(full[drop]);
    CHECK_FALSE(parses(missing));
  }
  std::vector<std::string> closure = full;
  closure.push_back("--closure=no-out");
  CHECK_FALSE(parses(closure));
  closure.back() = "--closure=" + toString(testCatalogGuid("scenes/s.scene.txt")) + "=c.txt";
  CHECK(parses(closure));
}

TEST_CASE("lookup reads a cooked catalog by GUID or by full source", "[asset_cooker][assemble_catalog][lookup]") {
  const ScratchDir dir("lookup");
  const CookedBuild build = cookSmallBuild(dir.path, pngFixture());
  const fs::path out = build.outDir / "asset_catalog.txt";
  REQUIRE(runArgs(assembleArgs(build, out)).exitCode == 0);
  const std::string catalogFlag = "--catalog=" + out.string();

  const Outcome byGuid = runArgs({"--kind=lookup", catalogFlag, "--guid=" + toString(build.scene)});
  REQUIRE(byGuid.exitCode == 0);
  CHECK(byGuid.out.rfind("record: guid=" + toString(build.scene) + " ", 0) == 0);
  CHECK(byGuid.out.find(" source=assets:scenes/s.scene.txt ") != std::string::npos);

  const Outcome bySource = runArgs({"--kind=lookup", catalogFlag, "--source=assets:textures/t.png"});
  REQUIRE(bySource.exitCode == 0);
  CHECK(bySource.out.find("guid=" + toString(testCatalogGuid("textures/t.png"))) != std::string::npos);

  CHECK(runArgs({"--kind=lookup", catalogFlag, "--source=assets:textures/absent.png"}).exitCode != 0);
  CHECK(runArgs({"--kind=lookup", catalogFlag, "--catalog-source=" + build.catalogSource.string(),
                 "--source=assets:textures/t.png"})
            .exitCode != 0);
  CHECK(runArgs({"--kind=assemble-catalog", catalogFlag, "--catalog-source=c", "--declarations=d",
                 "--fragment-list=f", "--out=o"})
            .exitCode != 0);
}

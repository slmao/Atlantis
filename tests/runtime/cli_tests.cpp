#include "../../src/runtime/cli.h"

#include <atlantis/assert.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::cli::CommandLineOutcome;
using atlantis::runtime::cli::CommandLineResult;
using atlantis::runtime::cli::kDefaultSceneName;
using atlantis::runtime::cli::parseCommandLine;
using atlantis::runtime::cli::SceneBootstrapPaths;
using atlantis::runtime::cli::SceneWhitelistEntry;

namespace {

// Owns argv[0] plus every supplied token, and a matching char* vector
// -- constructed once per TEST_CASE/SECTION and used immediately, so
// FakeArgv's own storage never reallocates or moves out from under
// `pointers` while a test holds it.
struct FakeArgv {
  std::vector<std::string> storage;
  std::vector<char*> pointers;

  explicit FakeArgv(std::initializer_list<std::string_view> args) {
    storage.emplace_back("atlantis_runtime");
    for (auto a : args) storage.emplace_back(a);
    pointers.reserve(storage.size());
    for (auto& s : storage) pointers.push_back(s.data());
  }

  [[nodiscard]] int argc() const { return static_cast<int>(pointers.size()); }
  [[nodiscard]] char** argv() { return pointers.data(); }
};

// Three fixture entries, fake/distinguishable paths only -- never real
// build paths (Spec 0032 Requirement 5). `suffix` lets a test build
// several independently-distinguishable whitelists.
[[nodiscard]] std::array<SceneWhitelistEntry, 3> makeFixtureWhitelist(std::string_view suffix = "") {
  return {{
      {"integrated_showcase_demo", SceneBootstrapPaths{std::string("/fake/a/artifact").append(suffix),
                                                         std::string("/fake/a/metadata").append(suffix),
                                                         std::string("/fake/a/manifest").append(suffix)}},
      {"ibl_material_demo", SceneBootstrapPaths{std::string("/fake/b/artifact").append(suffix),
                                                 std::string("/fake/b/metadata").append(suffix),
                                                 std::string("/fake/b/manifest").append(suffix)}},
      {"pbr_normal_map_demo", SceneBootstrapPaths{std::string("/fake/c/artifact").append(suffix),
                                                   std::string("/fake/c/metadata").append(suffix),
                                                   std::string("/fake/c/manifest").append(suffix)}},
  }};
}

[[nodiscard]] const SceneWhitelistEntry& findFixtureEntry(const std::array<SceneWhitelistEntry, 3>& whitelist,
                                                           std::string_view name) {
  for (const auto& entry : whitelist) {
    if (entry.name == name) return entry;
  }
  FAIL("test fixture bug: no entry named " << name);
  return whitelist[0];
}

void requireRunScene(const CommandLineResult& result, const std::array<SceneWhitelistEntry, 3>& whitelist,
                      std::string_view expectedName) {
  REQUIRE(result.outcome == CommandLineOutcome::RunScene);
  REQUIRE(result.message.empty());
  REQUIRE(result.selectedScene.has_value());
  const SceneWhitelistEntry& expected = findFixtureEntry(whitelist, expectedName);
  REQUIRE(result.selectedScene->sceneArtifactPath == expected.paths.sceneArtifactPath);
  REQUIRE(result.selectedScene->sceneMetadataPath == expected.paths.sceneMetadataPath);
  REQUIRE(result.selectedScene->sceneDependencyManifestPath == expected.paths.sceneDependencyManifestPath);
}

void requireUsage(const CommandLineResult& result, std::string_view expectedMessageSubstring) {
  REQUIRE(result.outcome == CommandLineOutcome::PrintUsageAndExit);
  REQUIRE_FALSE(result.selectedScene.has_value());
  REQUIRE(result.message.find(expectedMessageSubstring) != std::string::npos);
}

void requireError(const CommandLineResult& result, std::string_view expectedDiagnosticSubstring) {
  REQUIRE(result.outcome == CommandLineOutcome::PrintErrorAndExit);
  REQUIRE_FALSE(result.selectedScene.has_value());
  REQUIRE(result.message.find("atlantis_runtime: ") == 0);
  REQUIRE(result.message.find(expectedDiagnosticSubstring) != std::string::npos);
  REQUIRE(result.message.find("usage: atlantis_runtime") != std::string::npos);
}

}  // namespace

// ---------------------------------------------------------------------------
// Rows 1-4: RunScene (no argument, and each of the three whitelisted names).
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): no argument selects the default scene", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireRunScene(result, whitelist, kDefaultSceneName);
}

TEST_CASE("parseCommandLine(): --scene integrated_showcase_demo selects the default scene explicitly",
          "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "integrated_showcase_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireRunScene(result, whitelist, "integrated_showcase_demo");
}

TEST_CASE("parseCommandLine(): --scene ibl_material_demo selects that scene", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "ibl_material_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireRunScene(result, whitelist, "ibl_material_demo");
}

TEST_CASE("parseCommandLine(): --scene pbr_normal_map_demo selects that scene", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "pbr_normal_map_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireRunScene(result, whitelist, "pbr_normal_map_demo");
}

// ---------------------------------------------------------------------------
// Rows 5-6: --help / --list-scenes.
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): --help prints usage and exits", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireUsage(result, "usage: atlantis_runtime [--scene <name>]");
  REQUIRE(result.message.find("integrated_showcase_demo, ibl_material_demo, pbr_normal_map_demo") !=
          std::string::npos);
}

TEST_CASE("parseCommandLine(): --list-scenes prints the three names in whitelist order", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--list-scenes"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  REQUIRE(result.outcome == CommandLineOutcome::PrintUsageAndExit);
  REQUIRE(result.message == "integrated_showcase_demo\nibl_material_demo\npbr_normal_map_demo\n");
}

// ---------------------------------------------------------------------------
// Rows 7-15: single-argument error cases.
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): unrecognized --scene value is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "bogus_name"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "unrecognized --scene value: bogus_name");
}

TEST_CASE("parseCommandLine(): --scene with no following value is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--scene requires a value");
}

TEST_CASE("parseCommandLine(): duplicated --scene with the same value is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "ibl_material_demo", "--scene", "ibl_material_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--scene supplied more than once");
}

TEST_CASE("parseCommandLine(): duplicated --scene with different values is still an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "ibl_material_demo", "--scene", "pbr_normal_map_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--scene supplied more than once");
}

TEST_CASE("parseCommandLine(): duplicated --help is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--help", "--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help supplied more than once");
}

TEST_CASE("parseCommandLine(): duplicated --list-scenes is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--list-scenes", "--list-scenes"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--list-scenes supplied more than once");
}

TEST_CASE("parseCommandLine(): an unknown flag is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--foo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "unrecognized argument: --foo");
}

TEST_CASE("parseCommandLine(): the single-token --scene=<name> form is unsupported", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene=ibl_material_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "unrecognized argument: --scene=ibl_material_demo");
}

TEST_CASE("parseCommandLine(): a bare positional argument is an error", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"ibl_material_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "unrecognized argument: ibl_material_demo");
}

// ---------------------------------------------------------------------------
// Rows 16-17: --scene immediately followed by --help/--list-scenes -- these
// are consumed as the --scene *value* (cli.cpp never looks ahead), so they
// surface as an unrecognized --scene value, not a conflict or a missing-value
// error. See cli.cpp's own comment at this exact branch.
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): --scene --help is an unrecognized --scene value, not a conflict",
          "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "unrecognized --scene value: --help");
}

TEST_CASE("parseCommandLine(): --scene --list-scenes is an unrecognized --scene value, not a conflict",
          "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "--list-scenes"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "unrecognized --scene value: --list-scenes");
}

// ---------------------------------------------------------------------------
// Rows 18-23: conflicting flags, both orders for each of the three pairs.
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): --help --list-scenes conflict", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--help", "--list-scenes"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help, --list-scenes, and --scene may not be combined");
}

TEST_CASE("parseCommandLine(): --list-scenes --help conflict (reverse order)", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--list-scenes", "--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help, --list-scenes, and --scene may not be combined");
}

TEST_CASE("parseCommandLine(): --help --scene <name> conflict", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--help", "--scene", "ibl_material_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help, --list-scenes, and --scene may not be combined");
}

TEST_CASE("parseCommandLine(): --scene <name> --help conflict (reverse order)", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "ibl_material_demo", "--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help, --list-scenes, and --scene may not be combined");
}

TEST_CASE("parseCommandLine(): --list-scenes --scene <name> conflict", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--list-scenes", "--scene", "ibl_material_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help, --list-scenes, and --scene may not be combined");
}

TEST_CASE("parseCommandLine(): --scene <name> --list-scenes conflict (reverse order)", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "ibl_material_demo", "--list-scenes"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireError(result, "--help, --list-scenes, and --scene may not be combined");
}

// ---------------------------------------------------------------------------
// Additional coverage: three-different-whitelist mapping (no
// cross-contamination), default-compatibility, and the missing-default
// precondition -- per Plan 0032's own Testing & Verification Plan.
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): three independently-constructed whitelists each select their own paths, "
          "with no cross-contamination",
          "[runtime][cli]") {
  const auto whitelistOne = makeFixtureWhitelist("-w1");
  const auto whitelistTwo = makeFixtureWhitelist("-w2");
  const auto whitelistThree = makeFixtureWhitelist("-w3");

  for (const auto* name : {"integrated_showcase_demo", "ibl_material_demo", "pbr_normal_map_demo"}) {
    FakeArgv faOne{"--scene", name};
    requireRunScene(parseCommandLine(faOne.argc(), faOne.argv(), whitelistOne), whitelistOne, name);

    FakeArgv faTwo{"--scene", name};
    requireRunScene(parseCommandLine(faTwo.argc(), faTwo.argv(), whitelistTwo), whitelistTwo, name);

    FakeArgv faThree{"--scene", name};
    requireRunScene(parseCommandLine(faThree.argc(), faThree.argv(), whitelistThree), whitelistThree, name);
  }

  // The three whitelists must actually be distinguishable, or the loop
  // above would pass even with real cross-contamination.
  REQUIRE(findFixtureEntry(whitelistOne, "ibl_material_demo").paths.sceneArtifactPath !=
          findFixtureEntry(whitelistTwo, "ibl_material_demo").paths.sceneArtifactPath);
  REQUIRE(findFixtureEntry(whitelistTwo, "ibl_material_demo").paths.sceneArtifactPath !=
          findFixtureEntry(whitelistThree, "ibl_material_demo").paths.sceneArtifactPath);
}

TEST_CASE("parseCommandLine(): no-argument and explicit default --scene select byte-identical paths",
          "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();

  FakeArgv faDefault{};
  const auto defaultResult = parseCommandLine(faDefault.argc(), faDefault.argv(), whitelist);

  FakeArgv faExplicit{"--scene", "integrated_showcase_demo"};
  const auto explicitResult = parseCommandLine(faExplicit.argc(), faExplicit.argv(), whitelist);

  REQUIRE(defaultResult.outcome == CommandLineOutcome::RunScene);
  REQUIRE(explicitResult.outcome == CommandLineOutcome::RunScene);
  REQUIRE(defaultResult.selectedScene->sceneArtifactPath == explicitResult.selectedScene->sceneArtifactPath);
  REQUIRE(defaultResult.selectedScene->sceneMetadataPath == explicitResult.selectedScene->sceneMetadataPath);
  REQUIRE(defaultResult.selectedScene->sceneDependencyManifestPath ==
          explicitResult.selectedScene->sceneDependencyManifestPath);
}

// Verified via the same replaceable atlantis::assertions::setFailureHandler()
// mechanism tests/core/assert_tests.cpp establishes and
// tests/runtime/scene_extraction_tests.cpp already reuses for a
// Runtime-domain ATLANTIS_CHECK_MSG case.
TEST_CASE("parseCommandLine(): a whitelist missing the default scene entry fails fast via ATLANTIS_CHECK_MSG, "
          "then returns its defensive PrintErrorAndExit fallback",
          "[runtime][cli]") {
  const std::array<SceneWhitelistEntry, 2> whitelistWithoutDefault{{
      {"ibl_material_demo", SceneBootstrapPaths{"/fake/b/artifact", "/fake/b/metadata", "/fake/b/manifest"}},
      {"pbr_normal_map_demo", SceneBootstrapPaths{"/fake/c/artifact", "/fake/c/metadata", "/fake/c/manifest"}},
  }};

  int failureCount = 0;
  auto previous = atlantis::assertions::setFailureHandler(
      [&failureCount](const atlantis::AssertFailureInfo&) { ++failureCount; });

  FakeArgv fa{};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelistWithoutDefault);

  atlantis::assertions::setFailureHandler(std::move(previous));

  REQUIRE(failureCount == 1);
  REQUIRE(result.outcome == CommandLineOutcome::PrintErrorAndExit);
  REQUIRE_FALSE(result.selectedScene.has_value());
}

#include "../../src/runtime/cli.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_guid.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::cli::CommandLineOutcome;
using atlantis::runtime::cli::CommandLineResult;
using atlantis::runtime::cli::kDefaultSceneName;
using atlantis::runtime::cli::parseCommandLine;
using atlantis::runtime::cli::SceneSelection;
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

// A fake, deterministic, non-nil scene GUID per (key, suffix) -- never a real
// catalog GUID (Spec 0032 Requirement 5). `suffix` lets a test build several
// independently-distinguishable whitelists.
[[nodiscard]] atlantis::asset_system::AssetGuid fakeSceneGuid(std::string_view key, std::string_view suffix) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00320032-0032-4032-8032-003200320032").value(),
      std::string(key).append(suffix));
}

// Four fixture entries. Plan 0035 Milestone 5 (Spec 0035 Requirement 7): the
// 4th entry is purely additive -- the existing three entries' own fake GUIDs
// are unchanged.
[[nodiscard]] std::array<SceneWhitelistEntry, 4> makeFixtureWhitelist(std::string_view suffix = "") {
  return {{
      {"integrated_showcase_demo", SceneSelection{fakeSceneGuid("a", suffix)}},
      {"ibl_material_demo", SceneSelection{fakeSceneGuid("b", suffix)}},
      {"pbr_normal_map_demo", SceneSelection{fakeSceneGuid("c", suffix)}},
      {"pbr_materials_showcase", SceneSelection{fakeSceneGuid("d", suffix)}},
  }};
}

[[nodiscard]] const SceneWhitelistEntry& findFixtureEntry(const std::array<SceneWhitelistEntry, 4>& whitelist,
                                                           std::string_view name) {
  for (const auto& entry : whitelist) {
    if (entry.name == name) return entry;
  }
  FAIL("test fixture bug: no entry named " << name);
  return whitelist[0];
}

void requireRunScene(const CommandLineResult& result, const std::array<SceneWhitelistEntry, 4>& whitelist,
                      std::string_view expectedName) {
  REQUIRE(result.outcome == CommandLineOutcome::RunScene);
  REQUIRE(result.message.empty());
  REQUIRE(result.selectedScene.has_value());
  const SceneWhitelistEntry& expected = findFixtureEntry(whitelist, expectedName);
  REQUIRE(result.selectedScene->sceneAsset == expected.selection.sceneAsset);
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

// Plan 0035 Milestone 5 (Spec 0035 Requirement 7): the 4th whitelist
// entry's own name-to-path mapping, mirroring the three TEST_CASEs
// above exactly.
TEST_CASE("parseCommandLine(): --scene pbr_materials_showcase selects that scene", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--scene", "pbr_materials_showcase"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireRunScene(result, whitelist, "pbr_materials_showcase");
}

// ---------------------------------------------------------------------------
// Rows 5-6: --help / --list-scenes.
// ---------------------------------------------------------------------------

TEST_CASE("parseCommandLine(): --help prints usage and exits", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireUsage(result, "usage: atlantis_runtime [--scene <name>]");
  REQUIRE(result.message.find(
              "integrated_showcase_demo, ibl_material_demo, pbr_normal_map_demo, pbr_materials_showcase") !=
          std::string::npos);
}

TEST_CASE("parseCommandLine(): --list-scenes prints the four names in whitelist order", "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--list-scenes"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  REQUIRE(result.outcome == CommandLineOutcome::PrintUsageAndExit);
  REQUIRE(result.message == "integrated_showcase_demo\nibl_material_demo\npbr_normal_map_demo\npbr_materials_showcase\n");
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

  for (const auto* name :
       {"integrated_showcase_demo", "ibl_material_demo", "pbr_normal_map_demo", "pbr_materials_showcase"}) {
    FakeArgv faOne{"--scene", name};
    requireRunScene(parseCommandLine(faOne.argc(), faOne.argv(), whitelistOne), whitelistOne, name);

    FakeArgv faTwo{"--scene", name};
    requireRunScene(parseCommandLine(faTwo.argc(), faTwo.argv(), whitelistTwo), whitelistTwo, name);

    FakeArgv faThree{"--scene", name};
    requireRunScene(parseCommandLine(faThree.argc(), faThree.argv(), whitelistThree), whitelistThree, name);
  }

  // The three whitelists must actually be distinguishable, or the loop
  // above would pass even with real cross-contamination.
  REQUIRE(findFixtureEntry(whitelistOne, "ibl_material_demo").selection.sceneAsset !=
          findFixtureEntry(whitelistTwo, "ibl_material_demo").selection.sceneAsset);
  REQUIRE(findFixtureEntry(whitelistTwo, "ibl_material_demo").selection.sceneAsset !=
          findFixtureEntry(whitelistThree, "ibl_material_demo").selection.sceneAsset);
}

TEST_CASE("parseCommandLine(): no-argument and explicit default --scene select the identical scene",
          "[runtime][cli]") {
  const auto whitelist = makeFixtureWhitelist();

  FakeArgv faDefault{};
  const auto defaultResult = parseCommandLine(faDefault.argc(), faDefault.argv(), whitelist);

  FakeArgv faExplicit{"--scene", "integrated_showcase_demo"};
  const auto explicitResult = parseCommandLine(faExplicit.argc(), faExplicit.argv(), whitelist);

  REQUIRE(defaultResult.outcome == CommandLineOutcome::RunScene);
  REQUIRE(explicitResult.outcome == CommandLineOutcome::RunScene);
  REQUIRE(defaultResult.selectedScene->sceneAsset == explicitResult.selectedScene->sceneAsset);
}

// Verified via the same replaceable atlantis::assertions::setFailureHandler()
// mechanism tests/core/assert_tests.cpp establishes and
// tests/runtime/scene_extraction_tests.cpp already reuses for a
// Runtime-domain ATLANTIS_CHECK_MSG case.
TEST_CASE("parseCommandLine(): a whitelist missing the default scene entry fails fast via ATLANTIS_CHECK_MSG, "
          "then returns its defensive PrintErrorAndExit fallback",
          "[runtime][cli]") {
  const std::array<SceneWhitelistEntry, 2> whitelistWithoutDefault{{
      {"ibl_material_demo", SceneSelection{fakeSceneGuid("b", "")}},
      {"pbr_normal_map_demo", SceneSelection{fakeSceneGuid("c", "")}},
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

// ---------------------------------------------------------------------------
// Plan 0054 M5 (Spec 0054 ruling Q3, P7): --exec <path|->, a CLI script run
// against the selected scene. It modifies a RunScene result only.
// ---------------------------------------------------------------------------
TEST_CASE("parseCommandLine(): no --exec leaves execScript empty", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv args{};
  const CommandLineResult result = parseCommandLine(args.argc(), args.argv(), whitelist);
  requireRunScene(result, whitelist, "integrated_showcase_demo");
  REQUIRE_FALSE(result.execScript.has_value());
}

TEST_CASE("parseCommandLine(): --exec <path> runs the default scene with that script", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv args{"--exec", "tests/cli/scripts/north_star.txt"};
  const CommandLineResult result = parseCommandLine(args.argc(), args.argv(), whitelist);
  requireRunScene(result, whitelist, "integrated_showcase_demo");
  REQUIRE(result.execScript == std::optional<std::string>{"tests/cli/scripts/north_star.txt"});
}

TEST_CASE("parseCommandLine(): --exec - combines with --scene in either order", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv sceneFirst{"--scene", "ibl_material_demo", "--exec", "-"};
  const CommandLineResult first = parseCommandLine(sceneFirst.argc(), sceneFirst.argv(), whitelist);
  requireRunScene(first, whitelist, "ibl_material_demo");
  REQUIRE(first.execScript == std::optional<std::string>{"-"});
  FakeArgv execFirst{"--exec", "-", "--scene", "ibl_material_demo"};
  const CommandLineResult second = parseCommandLine(execFirst.argc(), execFirst.argv(), whitelist);
  requireRunScene(second, whitelist, "ibl_material_demo");
  REQUIRE(second.execScript == std::optional<std::string>{"-"});
}

TEST_CASE("parseCommandLine(): --exec errors -- missing value, repeated, combined with --help/--list-scenes",
          "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv missing{"--exec"};
  requireError(parseCommandLine(missing.argc(), missing.argv(), whitelist), "--exec requires a value");
  FakeArgv twice{"--exec", "a.txt", "--exec", "b.txt"};
  requireError(parseCommandLine(twice.argc(), twice.argv(), whitelist), "--exec supplied more than once");
  FakeArgv withHelp{"--exec", "a.txt", "--help"};
  requireError(parseCommandLine(withHelp.argc(), withHelp.argv(), whitelist),
               "--exec runs a scene; it may not be combined with --help or --list-scenes");
  FakeArgv withList{"--list-scenes", "--exec", "a.txt"};
  requireError(parseCommandLine(withList.argc(), withList.argv(), whitelist),
               "--exec runs a scene; it may not be combined with --help or --list-scenes");
}

TEST_CASE("parseCommandLine(): --help shows --exec", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv args{"--help"};
  requireUsage(parseCommandLine(args.argc(), args.argv(), whitelist),
               "usage: atlantis_runtime [--scene <name>] [--exec <script|->]");
}

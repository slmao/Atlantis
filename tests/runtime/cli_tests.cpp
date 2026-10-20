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

// Plan 0055 M2 (Spec 0055 R1, R10; P3, P4): --listen <port> serves attachable
// clients; --session-file <path> (only with --listen) places the session
// file. Without --listen there is no port, so no socket is ever opened.
TEST_CASE("parseCommandLine(): no --listen leaves listenPort and sessionFile empty", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv args{"--exec", "-"};
  const CommandLineResult result = parseCommandLine(args.argc(), args.argv(), whitelist);
  requireRunScene(result, whitelist, "integrated_showcase_demo");
  REQUIRE_FALSE(result.listenPort.has_value());
  REQUIRE_FALSE(result.sessionFile.has_value());
}

TEST_CASE("parseCommandLine(): --listen <port> and --session-file <path> combine with --scene and --exec",
          "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv ephemeral{"--listen", "0"};
  const CommandLineResult zero = parseCommandLine(ephemeral.argc(), ephemeral.argv(), whitelist);
  requireRunScene(zero, whitelist, "integrated_showcase_demo");
  REQUIRE(zero.listenPort == std::optional<std::uint16_t>{std::uint16_t{0}});
  REQUIRE_FALSE(zero.sessionFile.has_value());

  FakeArgv all{"--session-file", "s.json", "--scene", "ibl_material_demo", "--listen", "65535", "--exec", "-"};
  const CommandLineResult combined = parseCommandLine(all.argc(), all.argv(), whitelist);
  requireRunScene(combined, whitelist, "ibl_material_demo");
  REQUIRE(combined.listenPort == std::optional<std::uint16_t>{std::uint16_t{65535}});
  REQUIRE(combined.sessionFile == std::optional<std::string>{"s.json"});
  REQUIRE(combined.execScript == std::optional<std::string>{"-"});
}

TEST_CASE("parseCommandLine(): --listen and --session-file errors", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv missing{"--listen"};
  requireError(parseCommandLine(missing.argc(), missing.argv(), whitelist), "--listen requires a value");
  for (const std::string_view bad : {"65536", "-1", "abc", "1e3", "", "123456"}) {
    INFO(bad);
    FakeArgv args{"--listen", bad};
    requireError(parseCommandLine(args.argc(), args.argv(), whitelist), "invalid --listen port");
  }
  FakeArgv twice{"--listen", "1", "--listen", "2"};
  requireError(parseCommandLine(twice.argc(), twice.argv(), whitelist), "--listen supplied more than once");
  FakeArgv alone{"--session-file", "s.json"};
  requireError(parseCommandLine(alone.argc(), alone.argv(), whitelist), "--session-file requires --listen");
  FakeArgv noValue{"--listen", "0", "--session-file"};
  requireError(parseCommandLine(noValue.argc(), noValue.argv(), whitelist), "--session-file requires a value");
  FakeArgv withHelp{"--listen", "0", "--help"};
  requireError(parseCommandLine(withHelp.argc(), withHelp.argv(), whitelist),
               "--listen runs a scene; it may not be combined with --help or --list-scenes");
}

TEST_CASE("parseCommandLine(): --help shows --listen and --session-file", "[runtime][cli]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv args{"--help"};
  requireUsage(parseCommandLine(args.argc(), args.argv(), whitelist), "[--listen <port> [--session-file <path>]]");
}

// Plan 0056 M6 (Spec 0056 ruling Q2; P10, J9): `--editor` hosts the editor on
// the scene, alone or with --scene and --listen, and never with --exec,
// --help or --list-scenes.
TEST_CASE("parseCommandLine(): --editor combines with --scene and --listen", "[runtime][cli][editor]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv none{};
  CHECK_FALSE(parseCommandLine(none.argc(), none.argv(), whitelist).editor);
  FakeArgv alone{"--editor"};
  const CommandLineResult editor = parseCommandLine(alone.argc(), alone.argv(), whitelist);
  requireRunScene(editor, whitelist, "integrated_showcase_demo");
  CHECK(editor.editor);
  CHECK_FALSE(editor.listenPort.has_value());
  FakeArgv all{"--scene", "pbr_normal_map_demo", "--editor", "--listen", "0"};
  const CommandLineResult combined = parseCommandLine(all.argc(), all.argv(), whitelist);
  requireRunScene(combined, whitelist, "pbr_normal_map_demo");
  CHECK(combined.editor);
  CHECK(combined.listenPort == std::optional<std::uint16_t>{std::uint16_t{0}});
}

TEST_CASE("parseCommandLine(): --editor is refused with --exec, --help, --list-scenes, or twice",
          "[runtime][cli][editor]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv exec{"--editor", "--exec", "-"};
  requireError(parseCommandLine(exec.argc(), exec.argv(), whitelist), "--editor and --exec may not be combined");
  FakeArgv help{"--help", "--editor"};
  requireError(parseCommandLine(help.argc(), help.argv(), whitelist),
               "--editor runs a scene; it may not be combined with --help or --list-scenes");
  FakeArgv list{"--editor", "--list-scenes"};
  requireError(parseCommandLine(list.argc(), list.argv(), whitelist),
               "--editor runs a scene; it may not be combined with --help or --list-scenes");
  FakeArgv twice{"--editor", "--editor"};
  requireError(parseCommandLine(twice.argc(), twice.argv(), whitelist), "--editor supplied more than once");
  FakeArgv usage{"--help"};
  requireUsage(parseCommandLine(usage.argc(), usage.argv(), whitelist), "[--editor]");
}

// ---------------------------------------------------------------------------
// Plan 0060 M3 (ADR-0119 D5): --gate, --gate --record, --gate --applies, and the
// benchmark's additive --warmup-seconds.
// ---------------------------------------------------------------------------

using atlantis::runtime::cli::BenchmarkVariant;
using atlantis::runtime::cli::GateRequest;

namespace {

[[nodiscard]] const GateRequest& requireGate(const CommandLineResult& result) {
  REQUIRE(result.outcome == CommandLineOutcome::RunScene);
  REQUIRE(result.gate.has_value());
  REQUIRE_FALSE(result.selectedScene.has_value());
  REQUIRE_FALSE(result.benchmark.has_value());
  return *result.gate;
}

}  // namespace

TEST_CASE("parseCommandLine(): --gate alone is the protocol on bistro with every default", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--gate"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  const GateRequest& gate = requireGate(result);
  CHECK(gate.mode == GateRequest::Mode::Gate);
  CHECK(gate.sceneName == "bistro");
  CHECK_FALSE(gate.scene.has_value());  // the fixture whitelist has no bistro: reported as an environment mismatch
  CHECK(gate.variant == BenchmarkVariant::Baseline);
  CHECK(gate.width == 1920);
  CHECK(gate.height == 1080);
  CHECK_FALSE(gate.runs.has_value());
  CHECK_FALSE(gate.preheatSeconds.has_value());
  CHECK(gate.idleWait);
  CHECK_FALSE(gate.baselinesDir.has_value());
  CHECK_FALSE(gate.profileId.has_value());
  CHECK_FALSE(gate.policyPath.has_value());
  CHECK_FALSE(gate.json);
  CHECK_FALSE(gate.outputPath.has_value());
}

TEST_CASE("parseCommandLine(): --gate --scene names a whitelist scene", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--gate", "--scene", "integrated_showcase_demo"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  const GateRequest& gate = requireGate(result);
  CHECK(gate.sceneName == "integrated_showcase_demo");
  REQUIRE(gate.scene.has_value());
  CHECK(gate.scene->sceneAsset == whitelist[0].selection.sceneAsset);
}

TEST_CASE("parseCommandLine(): every --gate option is read", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--gate",      "--variant",   "no-fog",       "--resolution",     "1280x720", "--runs",
              "7",           "--baselines", "store",        "--profile",        "auto",     "--policy",
              "policy.json", "--json",      "--output",     "verdict.json",     "--idle-wait", "off",
              "--preheat-seconds", "2.5"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  const GateRequest& gate = requireGate(result);
  CHECK(gate.variant == BenchmarkVariant::NoFog);
  CHECK(gate.width == 1280);
  CHECK(gate.height == 720);
  REQUIRE(gate.runs.has_value());
  CHECK(*gate.runs == 7);
  CHECK(*gate.baselinesDir == "store");
  CHECK(*gate.profileId == "auto");
  CHECK(*gate.policyPath == "policy.json");
  CHECK(gate.json);
  CHECK(*gate.outputPath == "verdict.json");
  CHECK_FALSE(gate.idleWait);
  REQUIRE(gate.preheatSeconds.has_value());
  CHECK(*gate.preheatSeconds == 2.5);
}

TEST_CASE("parseCommandLine(): --gate may not be combined with any other mode", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  for (const std::string_view other : {"--help", "--list-scenes", "--editor"}) {
    FakeArgv fa{"--gate", other};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--gate may not be combined");
  }
  {
    FakeArgv fa{"--gate", "--benchmark", "integrated_showcase_demo"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--gate may not be combined");
  }
  {
    FakeArgv fa{"--gate", "--exec", "x"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--gate may not be combined");
  }
  {
    FakeArgv fa{"--gate", "--listen", "0"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--gate may not be combined");
  }
  {
    FakeArgv fa{"--gate", "--listen", "0", "--session-file", "s.json"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--gate may not be combined");
  }
}

TEST_CASE("parseCommandLine(): the benchmark options of its own are refused with --gate", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  {
    FakeArgv fa{"--gate", "--warmup", "5"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
  {
    FakeArgv fa{"--gate", "--frames", "5"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
  {
    FakeArgv fa{"--gate", "--baseline", "b.json"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
  {
    FakeArgv fa{"--gate", "--series"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
  {
    FakeArgv fa{"--gate", "--gpu-timing=off"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
  {
    FakeArgv fa{"--gate", "--warmup-seconds", "3"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
}

TEST_CASE("parseCommandLine(): the gate options of its own need --gate", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  for (const std::string_view flag :
       {"--runs", "--baselines", "--profile", "--policy", "--idle-wait", "--preheat-seconds", "--sessions",
        "--session-gap-minutes", "--reason-category", "--reason", "--record-to", "--applicability"}) {
    FakeArgv fa{flag, "1"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --gate");
  }
  FakeArgv record{"--record"};
  requireError(parseCommandLine(record.argc(), record.argv(), whitelist), "require --gate");
  FakeArgv applies{"--applies"};
  requireError(parseCommandLine(applies.argc(), applies.argv(), whitelist), "require --gate");
}

TEST_CASE("parseCommandLine(): invalid --gate values are usage errors", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  const struct {
    std::string_view flag;
    std::string_view value;
    std::string_view mention;
  } cases[] = {{"--runs", "0", "invalid --runs"},
               {"--runs", "101", "invalid --runs"},
               {"--runs", "five", "invalid --runs"},
               {"--preheat-seconds", "-1", "invalid --preheat-seconds"},
               {"--preheat-seconds", "abc", "invalid --preheat-seconds"},
               {"--preheat-seconds", "4000", "invalid --preheat-seconds"},
               {"--preheat-seconds", "1.2.3", "invalid --preheat-seconds"},
               {"--idle-wait", "maybe", "invalid --idle-wait"},
               {"--resolution", "1920", "invalid --resolution"},
               {"--variant", "no-everything", "unrecognized --variant"}};
  for (const auto& c : cases) {
    INFO(c.flag << " " << c.value);
    FakeArgv fa{"--gate", c.flag, c.value};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), c.mention);
  }
  FakeArgv twice{"--gate", "--runs", "3", "--runs", "4"};
  requireError(parseCommandLine(twice.argc(), twice.argv(), whitelist), "--runs supplied more than once");
  FakeArgv missing{"--gate", "--runs"};
  requireError(parseCommandLine(missing.argc(), missing.argv(), whitelist), "--runs requires a value");
}

TEST_CASE("parseCommandLine(): --gate --record needs a stated reason and a destination", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  {
    FakeArgv fa{"--gate",   "--record", "--record-to", "out", "--reason-category", "4",
                "--reason", "initial baseline for the reference profile"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    const GateRequest& gate = requireGate(result);
    CHECK(gate.mode == GateRequest::Mode::Record);
    CHECK(gate.sessions == 3);  // the defaults of ADR-0119 D6
    CHECK(gate.sessionGapMinutes == 10.0);
    CHECK(gate.reasonCategory == 4);
    CHECK(gate.reasonText == "initial baseline for the reference profile");
    CHECK(gate.recordTo == "out");
  }
  {
    FakeArgv fa{"--gate", "--record", "--record-to", "out", "--reason-category", "1", "--reason", "x",
                "--sessions", "2", "--session-gap-minutes", "0.5"};
    const auto parsed = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    const GateRequest& gate = requireGate(parsed);
    CHECK(gate.sessions == 2);
    CHECK(gate.sessionGapMinutes == 0.5);
  }
  // Each of the three is mandatory.
  {
    FakeArgv fa{"--gate", "--record", "--reason-category", "4", "--reason", "r"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--record requires");
  }
  {
    FakeArgv fa{"--gate", "--record", "--record-to", "out", "--reason", "r"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--record requires");
  }
  {
    FakeArgv fa{"--gate", "--record", "--record-to", "out", "--reason-category", "4"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--record requires");
  }
  // The category is one of the four reasons; the text is not empty.
  for (const std::string_view category : {"0", "5", "x"}) {
    FakeArgv fa{"--gate", "--record", "--record-to", "out", "--reason-category", category, "--reason", "r"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "invalid --reason-category");
  }
  {
    FakeArgv fa{"--gate", "--record", "--record-to", "out", "--reason-category", "4", "--reason", ""};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--reason must be");
  }
  {
    FakeArgv fa{"--gate", "--record", "--record-to", "out", "--reason-category", "4", "--reason", "r",
                "--sessions", "0"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "invalid --sessions");
  }
}

TEST_CASE("parseCommandLine(): the record options need --record", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  for (const std::string_view flag :
       {"--sessions", "--session-gap-minutes", "--reason-category", "--reason", "--record-to"}) {
    FakeArgv fa{"--gate", flag, "1"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --record");
  }
}

TEST_CASE("parseCommandLine(): --gate --applies reads paths and takes only --applicability", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  {
    FakeArgv fa{"--gate", "--applies"};
    const auto parsed = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    const GateRequest& gate = requireGate(parsed);
    CHECK(gate.mode == GateRequest::Mode::Applies);
    CHECK_FALSE(gate.applicabilityPath.has_value());
  }
  {
    FakeArgv fa{"--gate", "--applies", "--applicability", "a.txt"};
    const auto parsed = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    const GateRequest& gate = requireGate(parsed);
    CHECK(*gate.applicabilityPath == "a.txt");
  }
  {
    FakeArgv fa{"--gate", "--applies", "--runs", "3"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--applies takes only --applicability");
  }
  {
    FakeArgv fa{"--gate", "--applies", "--json"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--applies takes only --applicability");
  }
  {
    FakeArgv fa{"--gate", "--applies", "--record"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--record and --applies may not be combined");
  }
  {
    FakeArgv fa{"--gate", "--applicability", "a.txt"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--applicability requires --applies");
  }
}

TEST_CASE("parseCommandLine(): --warmup-seconds is the additive warmup duration of the benchmark",
          "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.benchmark.has_value());
    CHECK(result.benchmark->warmupSeconds == 0.0);  // the default: warmup frames only
  }
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo", "--warmup-seconds", "90"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.benchmark.has_value());
    CHECK(result.benchmark->warmupSeconds == 90.0);
  }
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo", "--warmup-seconds", "2.5"};
    CHECK(parseCommandLine(fa.argc(), fa.argv(), whitelist).benchmark->warmupSeconds == 2.5);
  }
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo", "--warmup-seconds", "-3"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "invalid --warmup-seconds");
  }
  {
    FakeArgv fa{"--warmup-seconds", "3"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
}

TEST_CASE("parseCommandLine(): --help lists the command lines of the gate", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  FakeArgv fa{"--help"};
  const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
  requireUsage(result, "usage: atlantis_runtime [--scene <name>]");  // the existing first line is untouched
  CHECK(result.message.find("atlantis_runtime --gate [--scene <name>]") != std::string::npos);
  CHECK(result.message.find("--gate --record --record-to <dir> --reason-category <1-4> --reason <text>") !=
        std::string::npos);
  CHECK(result.message.find("--gate --applies") != std::string::npos);
  CHECK(result.message.find("--warmup-seconds") != std::string::npos);
}

TEST_CASE("parseCommandLine(): --pin-process is the benchmark's and the gate's process pin", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.benchmark.has_value());
    CHECK_FALSE(result.benchmark->pinProcess);  // a plain benchmark is unpinned, as it always was
  }
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo", "--pin-process", "on"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.benchmark.has_value());
    CHECK(result.benchmark->pinProcess);
  }
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo", "--pin-process", "off"};
    CHECK_FALSE(parseCommandLine(fa.argc(), fa.argv(), whitelist).benchmark->pinProcess);
  }
  {
    FakeArgv fa{"--gate"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.gate.has_value());
    CHECK_FALSE(result.gate->pinProcess.has_value());  // the policy decides
  }
  {
    FakeArgv fa{"--gate", "--pin-process", "off"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.gate.has_value());
    REQUIRE(result.gate->pinProcess.has_value());
    CHECK_FALSE(*result.gate->pinProcess);
  }
  for (const char* bad : {"maybe", "1", ""}) {
    FakeArgv fa{"--gate", "--pin-process", bad};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "invalid --pin-process");
  }
  {
    FakeArgv fa{"--gate", "--pin-process", "on", "--pin-process", "off"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--pin-process supplied more than once");
  }
  {
    FakeArgv fa{"--pin-process", "on"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --benchmark");
  }
  {
    FakeArgv fa{"--gate", "--applies", "--pin-process", "on"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--applies takes only --applicability");
  }
}

TEST_CASE("parseCommandLine(): --require-mains is the gate's mains-power override", "[runtime][cli][gate]") {
  const auto whitelist = makeFixtureWhitelist();
  {
    FakeArgv fa{"--gate"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.gate.has_value());
    CHECK_FALSE(result.gate->requireMains.has_value());  // the policy decides
  }
  {
    FakeArgv fa{"--gate", "--require-mains", "off"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    REQUIRE(result.gate.has_value());
    REQUIRE(result.gate->requireMains.has_value());
    CHECK_FALSE(*result.gate->requireMains);
  }
  {
    FakeArgv fa{"--gate", "--record", "--require-mains", "on"};
    const auto result = parseCommandLine(fa.argc(), fa.argv(), whitelist);
    requireError(result, "--reason-category");  // it parses; the record options are what is missing
  }
  for (const char* bad : {"maybe", "1", ""}) {
    FakeArgv fa{"--gate", "--require-mains", bad};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "invalid --require-mains");
  }
  {
    FakeArgv fa{"--gate", "--require-mains", "on", "--require-mains", "off"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--require-mains supplied more than once");
  }
  {
    FakeArgv fa{"--require-mains", "off"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --gate");
  }
  {
    FakeArgv fa{"--benchmark", "integrated_showcase_demo", "--require-mains", "off"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "require --gate");
  }
  {
    FakeArgv fa{"--gate", "--applies", "--require-mains", "off"};
    requireError(parseCommandLine(fa.argc(), fa.argv(), whitelist), "--applies takes only --applicability");
  }
}

TEST_CASE("parseCommandLine(): --canary is the gate's GPU canary child", "[runtime][cli][canary]") {
  const auto whitelist = makeFixtureWhitelist();
  const auto parse = [&](std::initializer_list<std::string_view> args) {
    FakeArgv fa{args};
    return parseCommandLine(fa.argc(), fa.argv(), whitelist);
  };
  {
    const auto result = parse({"--canary"});
    REQUIRE(result.outcome == CommandLineOutcome::RunScene);
    REQUIRE(result.canary.has_value());
    CHECK_FALSE(result.canary->json);
    CHECK_FALSE(result.canary->pinProcess);
    CHECK_FALSE(result.canary->outputPath.has_value());
    CHECK_FALSE(result.benchmark.has_value());
    CHECK_FALSE(result.gate.has_value());
  }
  {
    const auto result = parse({"--canary", "--output", "c.json", "--json", "--pin-process", "on"});
    REQUIRE(result.canary.has_value());
    CHECK(result.canary->json);
    CHECK(result.canary->pinProcess);
    REQUIRE(result.canary->outputPath.has_value());
    CHECK(*result.canary->outputPath == "c.json");
  }
  requireError(parse({"--canary", "--benchmark", "integrated_showcase_demo"}), "--canary may not be combined with");
  requireError(parse({"--canary", "--gate"}), "--canary may not be combined with");
  requireError(parse({"--canary", "--help"}), "--canary may not be combined with");
  requireError(parse({"--canary", "--list-scenes"}), "--canary may not be combined with");
  requireError(parse({"--canary", "--editor"}), "--canary may not be combined with");
  requireError(parse({"--canary", "--scene", "integrated_showcase_demo"}), "--canary may not be combined with");
  requireError(parse({"--canary", "--variant", "baseline"}), "--canary takes only --output, --json and --pin-process");
  requireError(parse({"--canary", "--resolution", "1280x720"}), "--canary takes only --output, --json and --pin-process");
  requireError(parse({"--canary", "--warmup", "10"}), "--canary takes only --output, --json and --pin-process");
  requireError(parse({"--canary", "--runs", "5"}), "--canary takes only --output, --json and --pin-process");
  requireError(parse({"--canary", "--require-mains", "off"}), "--canary takes only --output, --json and --pin-process");
  requireError(parse({"--canary", "--canary"}), "--canary supplied more than once");
  requireError(parse({"--canary", "--pin-process", "maybe"}), "invalid --pin-process");
}

// Spec 0064 / ADR-0126 D2, ADR-0128 D2 (Plan 0064 M1): --parent-window takes a
// non-zero decimal handle, combines with --scene and --listen, and is refused
// with --editor; --no-pick-geometry opts a listening Runtime out of picking.
TEST_CASE("parseCommandLine(): --parent-window and --no-pick-geometry", "[runtime][cli][child_window]") {
  auto whitelist = makeFixtureWhitelist();
  FakeArgv none{};
  const CommandLineResult plain = parseCommandLine(none.argc(), none.argv(), whitelist);
  CHECK_FALSE(plain.parentWindow.has_value());
  CHECK_FALSE(plain.noPickGeometry);

  FakeArgv child{"--scene", "pbr_normal_map_demo", "--listen", "0", "--parent-window", "1311234", "--no-pick-geometry"};
  const CommandLineResult combined = parseCommandLine(child.argc(), child.argv(), whitelist);
  requireRunScene(combined, whitelist, "pbr_normal_map_demo");
  CHECK(combined.parentWindow == std::optional<std::uint64_t>{1311234});
  CHECK(combined.noPickGeometry);

  FakeArgv withEditor{"--editor", "--parent-window", "42"};
  requireError(parseCommandLine(withEditor.argc(), withEditor.argv(), whitelist),
               "--parent-window and --editor may not be combined");
  FakeArgv zero{"--parent-window", "0"};
  requireError(parseCommandLine(zero.argc(), zero.argv(), whitelist), "invalid --parent-window handle: 0");
  FakeArgv text{"--parent-window", "0x1f"};
  requireError(parseCommandLine(text.argc(), text.argv(), whitelist), "invalid --parent-window handle: 0x1f");
  FakeArgv missing{"--parent-window"};
  requireError(parseCommandLine(missing.argc(), missing.argv(), whitelist), "--parent-window requires a value");
  FakeArgv twice{"--parent-window", "1", "--parent-window", "2"};
  requireError(parseCommandLine(twice.argc(), twice.argv(), whitelist), "--parent-window supplied more than once");
  FakeArgv benchmark{"--benchmark", "bistro", "--parent-window", "1"};
  requireError(parseCommandLine(benchmark.argc(), benchmark.argv(), whitelist),
               "--parent-window and --no-pick-geometry run a scene");
}

#include "../../src/runtime/gate_applies.h"

#include "../../src/runtime/gate_run.h"
#include "gate_test_fakes.h"
#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

// Spec 0060 R9 / ADR-0118 D5 (Plan 0060 M8, P9): the applicability file and the helper that tells an
// agent whether a change needs the performance gate -- the matcher over a table of paths, the file's
// own shape, and `--gate --applies` over stdin. No device.

namespace {

namespace gate = atlantis::runtime::gate;
namespace cli = atlantis::runtime::cli;
namespace json = atlantis::connection::json;

gate::Applicability committed() {
  const auto loaded = gate::loadApplicability(std::string(ATLANTIS_PERFORMANCE_DIR) + "/applicability.txt");
  REQUIRE(loaded.isOk());
  return loaded.value();
}

bool applies(const gate::Applicability& a, const std::string& path) {
  const std::vector<std::string> one{path};
  return gate::evaluateApplicability(a, one).applies;
}

}  // namespace

TEST_CASE("a glob's star stays inside one directory, a double star crosses them", "[runtime][gate][applies]") {
  CHECK(gate::matchesGlob("src/world/**", "src/world/ecs/world.cpp"));
  CHECK(gate::matchesGlob("src/world/**", "src/world/x.h"));
  CHECK_FALSE(gate::matchesGlob("src/world/**", "src/worlds/x.h"));
  CHECK_FALSE(gate::matchesGlob("src/world/**", "src/runtime/x.h"));
  CHECK(gate::matchesGlob("src/*/x.h", "src/world/x.h"));
  CHECK_FALSE(gate::matchesGlob("src/*/x.h", "src/world/ecs/x.h"));
  CHECK(gate::matchesGlob("*.cpp", "main.cpp"));
  CHECK_FALSE(gate::matchesGlob("*.cpp", "src/main.cpp"));
  CHECK(gate::matchesGlob("CMakeLists.txt", "CMakeLists.txt"));
  CHECK_FALSE(gate::matchesGlob("CMakeLists.txt", "src/core/CMakeLists.txt"));
  CHECK(gate::matchesGlob("**/CMakeLists.txt", "CMakeLists.txt"));
  CHECK(gate::matchesGlob("**/CMakeLists.txt", "src/core/CMakeLists.txt"));
  CHECK(gate::matchesGlob("a?c", "abc"));
  CHECK_FALSE(gate::matchesGlob("a?c", "a/c"));
  CHECK_FALSE(gate::matchesGlob("a?c", "ac"));
}

TEST_CASE("the profiling pattern matches a name prefix through any directories", "[runtime][gate][applies]") {
  CHECK(gate::matchesGlob("src/core/**/profile*", "src/core/include/atlantis/profile.h"));
  CHECK(gate::matchesGlob("src/core/**/profile*", "src/core/include/atlantis/profile_recorder.h"));
  CHECK(gate::matchesGlob("src/core/**/profile*", "src/core/src/profile_stats.cpp"));
  CHECK_FALSE(gate::matchesGlob("src/core/**/profile*", "src/core/src/log.cpp"));
  CHECK_FALSE(gate::matchesGlob("src/core/**/profile*", "src/core/src/profiler/x.cpp"));  // a star never crosses a slash
}

TEST_CASE("paths are normalized before matching", "[runtime][gate][applies]") {
  CHECK(gate::normalizePath("src\\renderer\\renderer.cpp") == "src/renderer/renderer.cpp");
  CHECK(gate::normalizePath("./src/world/x.h") == "src/world/x.h");
  CHECK(gate::normalizePath("src/world/x.h") == "src/world/x.h");
  const auto a = committed();
  CHECK(applies(a, "src\\renderer\\src\\renderer.cpp"));
}

TEST_CASE("the applicability file parses its sections, comments and blanks", "[runtime][gate][applies]") {
  const auto parsed = gate::parseApplicability(
      "# a comment\n\ninclude:\n  src/a/**   # trailing comment\nsrc/b.cpp\n\nexclude:\ndocs/**\n");
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().include == std::vector<std::string>{"src/a/**", "src/b.cpp"});
  CHECK(parsed.value().exclude == std::vector<std::string>{"docs/**"});
  CHECK(gate::parseApplicability("include:\r\nsrc/a/**\r\n").isOk());  // CRLF is fine
}

TEST_CASE("a malformed applicability file is an error naming the line", "[runtime][gate][applies]") {
  CHECK(gate::parseApplicability("src/a/**\ninclude:\n").error().find("line 1") != std::string::npos);
  CHECK(gate::parseApplicability("include:\nsrc/a/**\nbogus:\nx\n").error().find("unknown section 'bogus:'") != std::string::npos);
  CHECK(gate::parseApplicability("exclude:\ndocs/**\n").error().find("no include patterns") != std::string::npos);
  CHECK(gate::parseApplicability("").isErr());
  CHECK(gate::loadApplicability(std::string(ATLANTIS_PERFORMANCE_DIR) + "/nope.txt").isErr());
}

TEST_CASE("the committed applicability file selects the frame path and exempts the rest", "[runtime][gate][applies]") {
  const auto a = committed();
  for (const char* path : {"src/renderer/src/renderer.cpp",
                           "src/render_graph/src/execution.cpp",
                           "src/rhi/include/atlantis/rhi/types.h",
                           "src/vulkan_backend/src/vulkan_device.cpp",
                           "src/runtime/src/runtime_application.cpp",
                           "src/runtime/benchmark_run.cpp",
                           "src/world/include/atlantis/world/light.h",
                           "shaders/pbr_direct_lit.slang",
                           "src/core/include/atlantis/profile.h",
                           "src/core/src/profile_recorder.cpp",
                           "CMakeLists.txt",
                           "cmake/CompilerWarnings.cmake",
                           "tests/performance/gate_policy.json",
                           "tests/performance/baselines/p/profile.json"}) {
    INFO(path);
    CHECK(applies(a, path));
  }
  for (const char* path : {"docs/specs/0060-performance-contract.md",
                           "docs/process/definition-of-done.md",
                           "README.md",
                           "android/app/build.gradle",
                           "src/editor/src/view/hierarchy.cpp",
                           "src/gameplay_sdk/include/atlantis/gameplay/world.h",
                           "src/csharp/Atlantis.Gameplay/World.cs",
                           "src/remote/src/server.cpp",
                           "src/cli/src/command.cpp",
                           "src/connection/include/atlantis/connection/json.h",
                           "tests/renderer/renderer_tests.cpp",
                           "tests/runtime/cli_tests.cpp",
                           ".github/PULL_REQUEST_TEMPLATE.md"}) {
    INFO(path);
    CHECK_FALSE(applies(a, path));
  }
}

TEST_CASE("a path that matches an include and an exclude is an include", "[runtime][gate][applies]") {
  const auto a = committed();
  // tests/performance/** is an include; tests/** is an exclude.
  CHECK(applies(a, "tests/performance/applicability.txt"));
  CHECK_FALSE(applies(a, "tests/image_regression/x.cpp"));
  const auto custom = gate::parseApplicability("include:\nsrc/a/**\nexclude:\nsrc/**\n");
  REQUIRE(custom.isOk());
  CHECK(applies(custom.value(), "src/a/x.cpp"));
  CHECK_FALSE(applies(custom.value(), "src/b/x.cpp"));
}

TEST_CASE("a change set applies when any of its paths does, and lists those", "[runtime][gate][applies]") {
  const auto a = committed();
  const std::vector<std::string> paths{"docs/x.md", "src/renderer/a.cpp", "", "tests/runtime/t.cpp", "shaders/b.slang"};
  const auto result = gate::evaluateApplicability(a, paths);
  CHECK(result.applies);
  CHECK(result.matched == std::vector<std::string>{"src/renderer/a.cpp", "shaders/b.slang"});
  const std::vector<std::string> docsOnly{"docs/x.md", "README.md"};
  const auto none = gate::evaluateApplicability(a, docsOnly);
  CHECK_FALSE(none.applies);
  CHECK(none.matched.empty());
  CHECK_FALSE(gate::evaluateApplicability(a, {}).applies);
}

TEST_CASE("the helper's document is applies and matched, nothing else", "[runtime][gate][applies]") {
  gate::AppliesResult result;
  result.applies = true;
  result.matched = {"src/world/x.h"};
  const json::Value doc = gate::toValue(result);
  CHECK(json::write(doc) == R"({"applies":true,"matched":["src/world/x.h"]})");
  CHECK(json::write(gate::toValue({})) == R"({"applies":false,"matched":[]})");
}

TEST_CASE("the applies mode reads changed paths on stdin and prints one document", "[runtime][gate][applies]") {
  const auto run = [](const std::string& stdinText, std::optional<std::string> file = std::nullopt) {
    cli::GateRequest request;
    request.mode = cli::GateRequest::Mode::Applies;
    request.applicabilityPath = file;
    gate_test::FakeClock clock;
    std::istringstream in(stdinText);
    std::ostringstream out;
    std::ostringstream err;
    gate::GateEnvironment env;
    env.clock = &clock;
    env.performanceDir = ATLANTIS_PERFORMANCE_DIR;
    env.in = &in;
    env.out = &out;
    env.err = &err;
    const int code = gate::runGateWith(request, env);
    return std::make_tuple(code, out.str(), err.str());
  };
  {
    const auto [code, out, err] = run("docs/a.md\nsrc/renderer/b.cpp\r\n\nsrc\\world\\c.h\n");
    CHECK(code == 0);
    CHECK(out == "{\"applies\":true,\"matched\":[\"src/renderer/b.cpp\",\"src/world/c.h\"]}\n");
    CHECK(err.empty());
  }
  {
    const auto [code, out, err] = run("docs/a.md\nREADME.md\n");
    CHECK(code == 0);  // it judges nothing: a docs-only change is exit 0 with applies false
    CHECK(out == "{\"applies\":false,\"matched\":[]}\n");
  }
  {
    const auto [code, out, err] = run("");
    CHECK(code == 0);
    CHECK(out == "{\"applies\":false,\"matched\":[]}\n");
  }
  {
    const auto [code, out, err] = run("src/x\n", std::string(ATLANTIS_PERFORMANCE_DIR) + "/missing.txt");
    CHECK(code == 1);  // an unreadable file is an initialization failure, never a quiet "no"
    CHECK(out.empty());
    CHECK_FALSE(err.empty());
  }
}

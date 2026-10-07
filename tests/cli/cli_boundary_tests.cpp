#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Plan 0054 M4 (Spec 0054 R8; rulings Q4, Q6; ADR-0105 D3): the CLI is an
// external client -- it reaches the World only through RuntimeConnection.
// Its sources include only the connection, World's access value types and a
// few Core/Asset System value headers, never a Runtime, ECS, Platform, RHI,
// Renderer or Vulkan header; its target links only Atlantis::Connection.
// Atlantis Connection's sources are held to the same include rule (plus the
// World schema it serves).

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The include targets of every .h/.cpp under `root`.
struct Include {
  std::string file;
  std::string target;
};
[[nodiscard]] std::vector<Include> includesUnder(const fs::path& root) {
  std::vector<Include> found;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const auto extension = entry.path().extension();
    if (!entry.is_regular_file() || (extension != ".h" && extension != ".cpp")) continue;
    const std::string source = readFile(entry.path());
    std::size_t at = 0;
    while ((at = source.find("#include", at)) != std::string::npos) {
      const auto open = source.find_first_of("<\"", at);
      const auto close = source.find_first_of(">\"", open + 1);
      found.push_back({entry.path().string(), source.substr(open + 1, close - open - 1)});
      at = close;
    }
  }
  return found;
}

// Atlantis headers must be on the allowlist; anything else must be a
// standard header (no directory, no ".h": <vector>, <cstdint>).
[[nodiscard]] std::vector<std::string> violations(const std::vector<Include>& includes,
                                                  const std::vector<std::string_view>& allowedPrefixes) {
  std::vector<std::string> out;
  for (const Include& include : includes) {
    const std::string_view target = include.target;
    bool allowed = false;
    if (target.starts_with("atlantis/")) {
      allowed = std::any_of(allowedPrefixes.begin(), allowedPrefixes.end(),
                            [&](std::string_view prefix) { return target.starts_with(prefix); });
    } else {
      allowed = target.find('/') == std::string_view::npos && !target.ends_with(".h");
    }
    if (!allowed) out.push_back(include.file + ": " + include.target);
  }
  return out;
}

const std::vector<std::string_view> kClientValueHeaders = {
    "atlantis/connection/",  "atlantis/world/access/", "atlantis/schema.h",
    "atlantis/result.h",     "atlantis/assert.h",      "atlantis/asset_system/asset_guid.h",
};

}  // namespace

TEST_CASE("Atlantis CLI includes nothing of Runtime, the ECS or the GPU stack", "[cli][module_boundary]") {
  std::vector<Include> includes = includesUnder(ATLANTIS_CLI_SOURCE_DIR);
  // Plan 0055 P10: src/cli/app/ is the `atlantis` executable, not the library;
  // it also includes Atlantis Remote's client header, and is held to its own
  // rule below.
  std::erase_if(includes, [](const Include& include) { return fs::path(include.file).parent_path().filename() == "app"; });
  REQUIRE(includes.size() >= 10);  // the scan is not vacuous
  std::vector<std::string_view> allowed = kClientValueHeaders;
  allowed.push_back("atlantis/cli/");
  const std::vector<std::string> bad = violations(includes, allowed);
  std::string text;
  for (const auto& v : bad) text += v + '\n';
  INFO(text);
  CHECK(bad.empty());

  // The rule bites: these would be refused.
  CHECK(violations({{"x", "atlantis/runtime/runtime_application.h"}, {"x", "atlantis/world/ecs/world.h"},
                    {"x", "atlantis/platform/platform.h"}, {"x", "atlantis/rhi/device.h"},
                    {"x", "atlantis/renderer/renderer.h"}, {"x", "vulkan/vulkan.h"}},
                   allowed)
            .size() == 6);
}

TEST_CASE("Atlantis CLI links Atlantis::Connection and nothing else", "[cli][module_boundary]") {
  const std::string cmake = readFile(fs::path(ATLANTIS_CLI_SOURCE_DIR) / "CMakeLists.txt");
  const auto start = cmake.find("target_link_libraries(atlantis_cli");
  REQUIRE(start != std::string::npos);
  const auto end = cmake.find(')', start);
  std::string block = cmake.substr(start, end - start);
  for (const std::string_view word :
       {"target_link_libraries(atlantis_cli", "PUBLIC", "PRIVATE", "Atlantis::Connection", "atlantis_compiler_warnings"}) {
    const auto at = block.find(word);
    REQUIRE(at != std::string::npos);
    block.erase(at, word.size());
  }
  CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
}

TEST_CASE("Atlantis Connection includes only World's access value types, the schema and Core",
          "[cli][module_boundary]") {
  const std::vector<Include> includes = includesUnder(ATLANTIS_CONNECTION_SOURCE_DIR);
  REQUIRE(includes.size() >= 10);
  std::vector<std::string_view> allowed = kClientValueHeaders;
  allowed.push_back("atlantis/world/world_schema.h");
  const std::vector<std::string> bad = violations(includes, allowed);
  std::string text;
  for (const auto& v : bad) text += v + '\n';
  INFO(text);
  CHECK(bad.empty());
}

// Plan 0055 P10 (Spec 0055 ruling Q6, X-a): the `atlantis` executable is the
// library plus Atlantis Remote's client half -- never its server half, and
// nothing of Runtime, the ECS or the GPU stack.
TEST_CASE("the atlantis executable includes the CLI, Connection and Remote's client half only",
          "[cli][module_boundary]") {
  std::vector<Include> includes = includesUnder(fs::path(ATLANTIS_CLI_SOURCE_DIR) / "app");
  REQUIRE(includes.size() >= 5);
  std::vector<std::string_view> allowed = kClientValueHeaders;
  allowed.push_back("atlantis/cli/");
  allowed.push_back("atlantis/remote/remote_client.h");
  const std::vector<std::string> bad = violations(includes, allowed);
  std::string text;
  for (const auto& v : bad) text += v + '\n';
  INFO(text);
  CHECK(bad.empty());
  CHECK(violations({{"x", "atlantis/remote/remote_server.h"}}, allowed).size() == 1);

  const std::string cmake = readFile(fs::path(ATLANTIS_CLI_SOURCE_DIR) / "CMakeLists.txt");
  const auto start = cmake.find("target_link_libraries(atlantis_cli_app");
  REQUIRE(start != std::string::npos);
  std::string block = cmake.substr(start, cmake.find(')', start) - start);
  for (const std::string_view word : {"target_link_libraries(atlantis_cli_app", "PRIVATE", "Atlantis::Cli",
                                      "Atlantis::RemoteClient", "atlantis_compiler_warnings"}) {
    const auto at = block.find(word);
    REQUIRE(at != std::string::npos);
    block.erase(at, word.size());
  }
  CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
}

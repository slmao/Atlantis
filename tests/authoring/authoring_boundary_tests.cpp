#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Plan 0065 M2 (ADR-0130 D2; the tests/cli/cli_boundary_tests.cpp pattern):
// Atlantis Authoring depends on Atlantis Asset System and Atlantis Connection
// only. Its sources include those modules' headers (Connection's interface
// names World access's value types), Core's value headers, its own headers and
// standard headers -- never a Runtime, World ECS, Platform, RHI, Renderer,
// Vulkan or OS header -- and its target links the two modules and nothing else.

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Include {
  std::string file;
  std::string target;
  bool quoted = false;
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
      found.push_back({entry.path().string(), source.substr(open + 1, close - open - 1), source[open] == '"'});
      at = close;
    }
  }
  return found;
}

const std::vector<std::string_view> kAllowed = {
    "atlantis/authoring/",  "atlantis/connection/", "atlantis/asset_system/", "atlantis/world/access/",
    "atlantis/schema.h",    "atlantis/result.h",     "atlantis/assert.h",
};

// Atlantis headers on the allowlist; a quoted header of the module's own
// (no directory); otherwise a standard header (no directory, no ".h").
[[nodiscard]] std::vector<std::string> violations(const std::vector<Include>& includes) {
  std::vector<std::string> out;
  for (const Include& include : includes) {
    const std::string_view target = include.target;
    bool allowed = false;
    if (target.starts_with("atlantis/")) {
      allowed = std::any_of(kAllowed.begin(), kAllowed.end(), [&](std::string_view p) { return target.starts_with(p); });
    } else if (include.quoted) {
      allowed = target.find('/') == std::string_view::npos &&
                fs::exists(fs::path(ATLANTIS_AUTHORING_SOURCE_DIR) / "src" / std::string(target));
    } else {
      allowed = target.find('/') == std::string_view::npos && !target.ends_with(".h");
    }
    if (!allowed) out.push_back(include.file + ": " + include.target);
  }
  return out;
}

}  // namespace

TEST_CASE("Atlantis Authoring includes nothing of Runtime, the ECS, the GPU stack or the OS",
          "[authoring][module_boundary]") {
  const std::vector<Include> includes = includesUnder(ATLANTIS_AUTHORING_SOURCE_DIR);
  REQUIRE(includes.size() >= 20);  // the scan is not vacuous
  const std::vector<std::string> bad = violations(includes);
  std::string text;
  for (const auto& v : bad) text += v + '\n';
  INFO(text);
  CHECK(bad.empty());

  // The rule bites.
  CHECK(violations({{"x", "atlantis/runtime/runtime_application.h"},
                    {"x", "atlantis/world/ecs/world.h"},
                    {"x", "atlantis/world/world_schema.h"},
                    {"x", "atlantis/platform/platform.h"},
                    {"x", "atlantis/rhi/device.h"},
                    {"x", "atlantis/renderer/renderer.h"},
                    {"x", "vulkan/vulkan.h"},
                    {"x", "windows.h"},
                    {"x", "unistd.h"},
                    {"x", "other/missing.h", true}})
            .size() == 10);
}

TEST_CASE("Atlantis Authoring links Atlantis::AssetSystem and Atlantis::Connection and nothing else",
          "[authoring][module_boundary]") {
  const std::string cmake = readFile(fs::path(ATLANTIS_AUTHORING_SOURCE_DIR) / "CMakeLists.txt");
  const auto start = cmake.find("target_link_libraries(atlantis_authoring");
  REQUIRE(start != std::string::npos);
  const auto end = cmake.find(')', start);
  std::string block = cmake.substr(start, end - start);
  for (const std::string_view word : {"target_link_libraries(atlantis_authoring", "PUBLIC", "PRIVATE",
                                      "Atlantis::AssetSystem", "Atlantis::Connection", "atlantis_compiler_warnings"}) {
    const auto at = block.find(word);
    REQUIRE(at != std::string::npos);
    block.erase(at, word.size());
  }
  CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
}

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Plan 0057 M1 (Spec 0057 R1, R6; ADR-0110 D1): Atlantis Gameplay SDK is an
// ordinary client. Its sources include only the connection, World's access
// value types, Core's schema and Result, the GUID value header and their own
// headers -- never World's schema table or component types, the ECS,
// Runtime, Platform, RHI, Renderer, Vulkan, Remote or OS headers; its target
// links Atlantis::Connection only; and nothing in it names a byte offset.
// (After the Spec 0056 editor boundary scan.)

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Include {
  fs::path file;
  std::string target;
};

[[nodiscard]] std::vector<fs::path> sourcesUnder(const fs::path& root) {
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const auto extension = entry.path().extension();
    if (entry.is_regular_file() && (extension == ".h" || extension == ".cpp")) files.push_back(entry.path());
  }
  return files;
}

[[nodiscard]] std::vector<Include> includesUnder(const fs::path& root) {
  std::vector<Include> found;
  for (const fs::path& file : sourcesUnder(root)) {
    const std::string source = readFile(file);
    std::size_t at = 0;
    while ((at = source.find("#include", at)) != std::string::npos) {
      const auto open = source.find_first_of("<\"", at);
      const auto close = source.find_first_of(">\"", open + 1);
      found.push_back({file, source.substr(open + 1, close - open - 1)});
      at = close;
    }
  }
  return found;
}

const std::vector<std::string_view> kAllowedAtlantisPrefixes = {
    "atlantis/connection/", "atlantis/world/access/",           "atlantis/schema.h",
    "atlantis/result.h",    "atlantis/asset_system/asset_guid.h", "atlantis/gameplay/",
};

// Atlantis headers on the allowlist; a quoted ".h" include one of the
// library's own private headers; anything else a standard header.
[[nodiscard]] std::vector<std::string> violations(const std::vector<Include>& includes) {
  std::vector<std::string> out;
  for (const Include& include : includes) {
    const std::string_view target = include.target;
    bool allowed = false;
    if (target.starts_with("atlantis/")) {
      allowed = std::any_of(kAllowedAtlantisPrefixes.begin(), kAllowedAtlantisPrefixes.end(),
                            [&](std::string_view prefix) { return target.starts_with(prefix); });
    } else if (target.ends_with(".h")) {
      allowed = fs::exists(include.file.parent_path() / fs::path(std::string(target)));
    } else {
      allowed = target.find('/') == std::string_view::npos;
    }
    if (!allowed) out.push_back(include.file.generic_string() + ": " + include.target);
  }
  return out;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
  std::string text;
  for (const auto& line : lines) text += line + '\n';
  return text;
}

}  // namespace

TEST_CASE("Atlantis Gameplay SDK includes only the connection and its value types",
          "[gameplay_sdk][module_boundary]") {
  const fs::path root = ATLANTIS_GAMEPLAY_SDK_SOURCE_DIR;
  const std::vector<Include> includes = includesUnder(root);
  REQUIRE(includes.size() >= 10);  // the scan is not vacuous
  const std::vector<std::string> bad = violations(includes);
  INFO(joined(bad));
  CHECK(bad.empty());

  // The rule bites: each of these would be refused anywhere in the library.
  const fs::path someFile = root / "src" / "error.cpp";
  CHECK(violations({{someFile, "atlantis/world/world_schema.h"},
                    {someFile, "atlantis/world/light.h"},
                    {someFile, "atlantis/world/ecs/world.h"},
                    {someFile, "atlantis/runtime/runtime_application.h"},
                    {someFile, "atlantis/remote/remote_client.h"},
                    {someFile, "atlantis/platform/platform.h"},
                    {someFile, "atlantis/rhi/device.h"},
                    {someFile, "atlantis/renderer/renderer.h"},
                    {someFile, "vulkan/vulkan.h"},
                    {someFile, "windows.h"}})
            .size() == 10);
}

TEST_CASE("Atlantis Gameplay SDK names no byte offset and no World C++ type (R6)", "[gameplay_sdk][module_boundary]") {
  const fs::path root = ATLANTIS_GAMEPLAY_SDK_SOURCE_DIR;
  const std::vector<fs::path> files = sourcesUnder(root);
  REQUIRE(files.size() >= 4);
  std::vector<std::string> bad;
  for (const fs::path& file : files) {
    const std::string source = readFile(file);
    if (source.find("byteOffset") != std::string::npos) bad.push_back(file.generic_string() + ": byteOffset");
    // World's access value types (atlantis::world::access) are the
    // connection's own vocabulary; nothing else of World is named.
    for (std::size_t at = source.find("atlantis::world::"); at != std::string::npos;
         at = source.find("atlantis::world::", at + 1)) {
      const std::size_t after = at + 23;  // past "atlantis::world::access"
      const bool access = source.compare(at, 23, "atlantis::world::access") == 0 &&
                          (after == source.size() || source[after] == ':' || source[after] == ';');
      if (!access) {
        bad.push_back(file.generic_string() + ": " + source.substr(at, 40));
      }
    }
  }
  INFO(joined(bad));
  CHECK(bad.empty());
}

TEST_CASE("Atlantis Gameplay SDK links Atlantis::Connection and nothing else", "[gameplay_sdk][module_boundary]") {
  const std::string cmake = readFile(fs::path(ATLANTIS_GAMEPLAY_SDK_SOURCE_DIR) / "CMakeLists.txt");
  const auto start = cmake.find("target_link_libraries(atlantis_gameplay_sdk");
  REQUIRE(start != std::string::npos);
  REQUIRE(cmake.find("target_link_libraries(atlantis_gameplay_sdk", start + 1) == std::string::npos);
  const auto end = cmake.find(')', start);
  std::string block = cmake.substr(start, end - start);
  for (const std::string_view word : {"target_link_libraries(atlantis_gameplay_sdk", "PUBLIC", "PRIVATE",
                                      "Atlantis::Connection", "atlantis_compiler_warnings"}) {
    const auto at = block.find(word);
    REQUIRE(at != std::string::npos);
    block.erase(at, word.size());
  }
  CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
}

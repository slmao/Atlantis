#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Plan 0056 P8 (Spec 0056 R1, ruling Q7; ADR-0107, ADR-0108 D2): Atlantis
// Editor is an ordinary client. Its sources include only the connection,
// World's access value types, a few Core/Asset System value headers, their own
// headers and the UI library -- never a Runtime, ECS, Platform, RHI, Renderer,
// Vulkan or OS header; UI-library headers appear only under src/view/ (the
// public headers and the models are UI-free); its target links only
// Atlantis::Connection and the UI library; and the UI library is built from
// its four core sources, no backend.

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

// The include targets of every .h/.cpp under `root`.
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
      found.push_back({entry.path(), source.substr(open + 1, close - open - 1)});
      at = close;
    }
  }
  return found;
}

[[nodiscard]] bool isUiLibraryHeader(std::string_view target) {
  return target.starts_with("imgui") || target.starts_with("imstb") || target.find("backends/") != std::string_view::npos;
}

[[nodiscard]] bool isUnder(const fs::path& file, const fs::path& directory) {
  const std::string f = fs::weakly_canonical(file).generic_string();
  const std::string d = fs::weakly_canonical(directory).generic_string() + "/";
  return f.starts_with(d);
}

const std::vector<std::string_view> kAllowedAtlantisPrefixes = {
    "atlantis/connection/", "atlantis/world/access/", "atlantis/schema.h",
    "atlantis/result.h",    "atlantis/assert.h",      "atlantis/asset_system/asset_guid.h",
    "atlantis/editor/",
};

// Atlantis headers must be on the allowlist; a quoted include must be one of
// the library's own private headers (it resolves next to the including file);
// UI-library headers only under src/view/; anything else must be a standard
// header (no directory, no ".h").
[[nodiscard]] std::vector<std::string> violations(const std::vector<Include>& includes, const fs::path& viewDir) {
  std::vector<std::string> out;
  for (const Include& include : includes) {
    const std::string_view target = include.target;
    bool allowed = false;
    if (target.starts_with("atlantis/")) {
      allowed = std::any_of(kAllowedAtlantisPrefixes.begin(), kAllowedAtlantisPrefixes.end(),
                            [&](std::string_view prefix) { return target.starts_with(prefix); });
    } else if (isUiLibraryHeader(target)) {
      allowed = !viewDir.empty() && isUnder(include.file, viewDir) && target.find('/') == std::string_view::npos;
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

TEST_CASE("Atlantis Editor includes nothing of Runtime, the ECS, the GPU stack, Platform or the OS",
          "[editor][module_boundary]") {
  const fs::path root = ATLANTIS_EDITOR_SOURCE_DIR;
  const std::vector<Include> includes = includesUnder(root);
  REQUIRE(includes.size() >= 10);  // the scan is not vacuous
  const std::vector<std::string> bad = violations(includes, root / "src" / "view");
  INFO(joined(bad));
  CHECK(bad.empty());

  // The rule bites: these would be refused anywhere in the library.
  const fs::path someFile = root / "src" / "editor.cpp";
  CHECK(violations({{someFile, "atlantis/runtime/runtime_application.h"},
                    {someFile, "atlantis/world/ecs/world.h"},
                    {someFile, "atlantis/platform/platform.h"},
                    {someFile, "atlantis/rhi/device.h"},
                    {someFile, "atlantis/renderer/renderer.h"},
                    {someFile, "vulkan/vulkan.h"},
                    {someFile, "windows.h"},
                    {someFile, "imgui.h"}},
                   root / "src" / "view")
            .size() == 8);
}

TEST_CASE("Atlantis Editor's public headers and models include no UI-library header", "[editor][module_boundary]") {
  const fs::path root = ATLANTIS_EDITOR_SOURCE_DIR;
  std::vector<Include> includes = includesUnder(root / "include");
  if (fs::exists(root / "src" / "model")) {
    const std::vector<Include> model = includesUnder(root / "src" / "model");
    includes.insert(includes.end(), model.begin(), model.end());
  }
  REQUIRE(includes.size() >= 5);
  std::vector<std::string> bad;
  for (const Include& include : includes) {
    if (isUiLibraryHeader(include.target)) bad.push_back(include.file.generic_string() + ": " + include.target);
  }
  INFO(joined(bad));
  CHECK(bad.empty());

  // And outside src/view/ nothing names the UI library at all.
  std::vector<Include> nonView = includesUnder(root);
  std::erase_if(nonView, [&](const Include& include) { return isUnder(include.file, root / "src" / "view"); });
  bad.clear();
  for (const Include& include : nonView) {
    if (isUiLibraryHeader(include.target)) bad.push_back(include.file.generic_string() + ": " + include.target);
  }
  INFO(joined(bad));
  CHECK(bad.empty());
}

TEST_CASE("Atlantis Editor links Atlantis::Connection and the UI library and nothing else",
          "[editor][module_boundary]") {
  const std::string cmake = readFile(fs::path(ATLANTIS_EDITOR_SOURCE_DIR) / "CMakeLists.txt");
  const auto start = cmake.find("target_link_libraries(atlantis_editor");
  REQUIRE(start != std::string::npos);
  REQUIRE(cmake.find("target_link_libraries(atlantis_editor", start + 1) == std::string::npos);
  const auto end = cmake.find(')', start);
  std::string block = cmake.substr(start, end - start);
  for (const std::string_view word : {"target_link_libraries(atlantis_editor", "PUBLIC", "PRIVATE",
                                      "Atlantis::Connection", "ImGui::ImGui", "atlantis_compiler_warnings"}) {
    const auto at = block.find(word);
    REQUIRE(at != std::string::npos);
    block.erase(at, word.size());
  }
  CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
}

TEST_CASE("the UI library is built from its four core sources only, no backend", "[editor][module_boundary]") {
  const std::string cmake = readFile(ATLANTIS_IMGUI_CMAKE);
  const auto start = cmake.find("add_library(atlantis_imgui STATIC");
  REQUIRE(start != std::string::npos);
  std::string block = cmake.substr(start, cmake.find(')', start) - start);
  for (const std::string_view word :
       {"add_library(atlantis_imgui STATIC", "${imgui_SOURCE_DIR}/imgui.cpp", "${imgui_SOURCE_DIR}/imgui_draw.cpp",
        "${imgui_SOURCE_DIR}/imgui_tables.cpp", "${imgui_SOURCE_DIR}/imgui_widgets.cpp"}) {
    const auto at = block.find(word);
    REQUIRE(at != std::string::npos);
    block.erase(at, word.size());
  }
  CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
  // No CMake statement names a backend (only comments do).
  std::size_t lineStart = 0;
  while (lineStart < cmake.size()) {
    const std::size_t lineEnd = std::min(cmake.find('\n', lineStart), cmake.size());
    const std::string_view line = std::string_view(cmake).substr(lineStart, lineEnd - lineStart);
    const std::size_t first = line.find_first_not_of(" \t");
    if (first != std::string_view::npos && line[first] != '#') {
      INFO(std::string(line));
      CHECK(line.find("backends") == std::string_view::npos);
    }
    lineStart = lineEnd + 1;
  }
  CHECK(cmake.find("IMGUI_DISABLE_OBSOLETE_FUNCTIONS") != std::string::npos);
  CHECK(cmake.find("URL_HASH SHA256=") != std::string::npos);
  CHECK(cmake.find("v1.92.9b") != std::string::npos);
}

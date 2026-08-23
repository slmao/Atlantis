#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Plan 0012 V9 / ADR-0043: Atlantis Asset System depends on Atlantis
// Core only. This test enumerates every .h/.cpp under src/asset_system/
// at test-run time (not compile time), so it automatically covers every
// file later Plan 0012 steps add, with no further edits to this test.
// Plan 0015 Section D1/ADR-0053: "atlantis/world/" added to the
// forbidden list -- AssetSystem must never depend on World (the
// reverse dependency, World -> AssetSystem for AssetId, is the
// already-Accepted direction, ADR-0048); this constraint previously
// existed only as a design rule, not an automated check, since World
// did not exist when this test was first written (Plan 0012).

namespace {

constexpr std::array<const char*, 9> kForbiddenIncludePrefixes = {
    "atlantis/rhi/",       "atlantis/renderer/",       "atlantis/render_graph/", "atlantis/shader_system/",
    "atlantis/platform/",  "atlantis/vulkan_backend/", "atlantis/tools/",        "atlantis/world/",
    "vulkan/",
};

[[nodiscard]] std::vector<std::string> forbiddenIncludeLinesIn(const std::filesystem::path& file) {
  std::ifstream in(file);
  std::vector<std::string> hits;
  std::string line;
  while (std::getline(in, line)) {
    const auto hashPos = line.find('#');
    if (hashPos == std::string::npos) continue;
    if (line.find("include", hashPos) == std::string::npos) continue;
    for (const char* prefix : kForbiddenIncludePrefixes) {
      if (line.find(prefix) != std::string::npos) {
        hits.push_back(line);
        break;
      }
    }
  }
  return hits;
}

}  // namespace

TEST_CASE(
    "Asset System sources include no RHI/Renderer/RenderGraph/ShaderSystem/Platform/VulkanBackend/Tools/Vulkan "
    "header",
    "[asset_system][module_boundary]") {
  const std::filesystem::path root{ATLANTIS_ASSET_SYSTEM_SOURCE_DIR};
  REQUIRE(std::filesystem::exists(root));

  std::vector<std::string> violations;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const auto ext = entry.path().extension();
    if (ext != ".h" && ext != ".cpp") continue;
    for (const auto& hit : forbiddenIncludeLinesIn(entry.path())) {
      violations.push_back(entry.path().string() + ": " + hit);
    }
  }

  std::string violationsText;
  for (const auto& v : violations) {
    violationsText += v;
    violationsText += '\n';
  }
  INFO(violationsText);
  REQUIRE(violations.empty());
}

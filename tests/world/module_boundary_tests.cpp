#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Plan 0014 V16 / ADR-0048: Atlantis::World depends on Atlantis::Core
// and, narrowly, Atlantis::AssetSystem (for AssetId only) -- no RHI,
// Renderer, RenderGraph, ShaderSystem, Platform, VulkanBackend, Runtime,
// or Tools dependency. This test enumerates every .h/.cpp under
// src/world/ at test-run time (not compile time), so it automatically
// covers every file later Plan 0014 steps add, with no further edits to
// this test.

namespace {

constexpr std::array<const char*, 8> kForbiddenIncludePrefixes = {
    "atlantis/rhi/",      "atlantis/renderer/",       "atlantis/render_graph/", "atlantis/shader_system/",
    "atlantis/platform/", "atlantis/vulkan_backend/", "atlantis/runtime/",      "vulkan/",
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
    "World sources include no RHI/Renderer/RenderGraph/ShaderSystem/Platform/VulkanBackend/Runtime/Vulkan header",
    "[world][module_boundary]") {
  const std::filesystem::path root{ATLANTIS_WORLD_SOURCE_DIR};
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

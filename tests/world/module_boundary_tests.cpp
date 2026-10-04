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

// Plan 0047 M6 (P17, ADR-0097 D5): EntityGuid is the Asset System's identity
// type; it reaches Atlantis::World only through SceneEntityMap, declared in
// scene_instantiation.h. No other World header names it, so World's component
// types and EntityId stay free of it.
TEST_CASE("EntityGuid appears in World headers only in scene_instantiation.h", "[world][module_boundary]") {
  const std::filesystem::path root{ATLANTIS_WORLD_SOURCE_DIR};
  REQUIRE(std::filesystem::exists(root));

  bool sceneInstantiationNamesIt = false;
  std::vector<std::string> violations;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".h") continue;
    std::ifstream in(entry.path());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool names = text.find("EntityGuid") != std::string::npos;
    if (entry.path().filename() == "scene_instantiation.h") {
      sceneInstantiationNamesIt = names;
    } else if (names) {
      violations.push_back(entry.path().string());
    }
  }

  std::string violationsText;
  for (const auto& v : violations) violationsText += v + '\n';
  INFO(violationsText);
  CHECK(violations.empty());
  CHECK(sceneInstantiationNamesIt);  // the scan is not vacuous
}

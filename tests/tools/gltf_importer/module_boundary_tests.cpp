#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ADR-0084: atlantis_gltf_importer_lib depends on Asset System and Core only,
// and keeps cgltf private -- no importer header may include cgltf.h.
// Mirrors tests/asset_system/module_boundary_tests.cpp.

namespace {

constexpr std::array<const char*, 10> kForbiddenIncludePrefixes = {
    "atlantis/rhi/",           "atlantis/renderer/",   "atlantis/render_graph/", "atlantis/shader_system/",
    "atlantis/platform/",      "atlantis/world/",      "atlantis/runtime/",      "atlantis/vulkan_backend/",
    "vulkan/",                 "cook_command.h",
};

[[nodiscard]] bool isIncludeLine(const std::string& line) {
  const auto hashPos = line.find('#');
  return hashPos != std::string::npos && line.find("include", hashPos) != std::string::npos;
}

[[nodiscard]] std::vector<std::string> includeLinesMatching(const std::filesystem::path& file,
                                                          const std::vector<std::string>& needles) {
  std::ifstream in(file);
  std::vector<std::string> hits;
  std::string line;
  while (std::getline(in, line)) {
    if (!isIncludeLine(line)) continue;
    for (const std::string& needle : needles) {
      if (line.find(needle) != std::string::npos) {
        hits.push_back(file.filename().string() + ": " + line);
        break;
      }
    }
  }
  return hits;
}

}  // namespace

TEST_CASE("glTF importer sources include no module outside Asset System/Core, and no header includes cgltf",
          "[gltf_importer][module_boundary]") {
  const std::filesystem::path root{ATLANTIS_GLTF_IMPORTER_SOURCE_DIR};
  REQUIRE(std::filesystem::is_directory(root));
  const std::vector<std::string> forbidden(kForbiddenIncludePrefixes.begin(), kForbiddenIncludePrefixes.end());

  std::vector<std::string> hits;
  std::size_t scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    const std::string extension = entry.path().extension().string();
    if (extension != ".h" && extension != ".cpp") continue;
    ++scanned;
    for (const std::string& hit : includeLinesMatching(entry.path(), forbidden)) hits.push_back(hit);
    if (extension == ".h") {
      for (const std::string& hit : includeLinesMatching(entry.path(), {"cgltf.h"})) hits.push_back(hit);
    }
  }
  CHECK(scanned >= 3);
  INFO("offending includes: " << hits.size());
  for (const std::string& hit : hits) UNSCOPED_INFO(hit);
  CHECK(hits.empty());
}

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// Plan 0048 M1 (Spec 0048 Testing plan, ADR-0099 D1): Atlantis Core depends
// on nothing in Atlantis. Core's own public headers are flat
// (<atlantis/result.h>), so any include of <atlantis/<dir>/...> is another
// module's header. Scans every .h/.cpp under src/core at test-run time,
// following tests/asset_system/module_boundary_tests.cpp.

namespace {

[[nodiscard]] bool isForbiddenInclude(std::string_view line) {
  const auto hashPos = line.find('#');
  if (hashPos == std::string_view::npos || line.find("include", hashPos) == std::string_view::npos) return false;
  if (line.find("vulkan/") != std::string_view::npos) return true;
  const auto modulePos = line.find("atlantis/");
  if (modulePos == std::string_view::npos) return false;
  const std::string_view rest = line.substr(modulePos + std::string_view{"atlantis/"}.size());
  const auto end = rest.find_first_of(">\"");
  return rest.substr(0, end).find('/') != std::string_view::npos;
}

}  // namespace

TEST_CASE("Core include scan recognizes another module's header", "[core][module_boundary]") {
  CHECK(isForbiddenInclude("#include <atlantis/world/camera.h>"));
  CHECK(isForbiddenInclude("#include \"atlantis/asset_system/asset_id.h\""));
  CHECK(isForbiddenInclude("#include <vulkan/vulkan.h>"));
  CHECK_FALSE(isForbiddenInclude("#include <atlantis/result.h>"));
  CHECK_FALSE(isForbiddenInclude("#include <string_view>"));
}

TEST_CASE("Core sources include no other Atlantis module's header", "[core][module_boundary]") {
  const std::filesystem::path root{ATLANTIS_CORE_SOURCE_DIR};
  REQUIRE(std::filesystem::exists(root));

  std::string violations;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const auto ext = entry.path().extension();
    if (ext != ".h" && ext != ".cpp") continue;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      if (isForbiddenInclude(line)) violations += entry.path().string() + ": " + line + '\n';
    }
  }
  INFO(violations);
  REQUIRE(violations.empty());
}

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

// Spec 0059 R1/R2, ADR-0114 D2: (1) no public header of any module tests the
// three profiling build options -- which is what keeps struct layouts and
// vtables identical across configurations A/B/C; (2) the profile headers
// include no Tracy and no Vulkan header. Scans at test-run time, following
// module_boundary_tests.cpp.

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string readAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] bool isProfileHeader(const fs::path& path) {
  const std::string name = path.filename().string();
  return name == "profile.h" || name == "profile_recorder.h" || name == "profile_stats.h";
}

}  // namespace

TEST_CASE("no public header outside the profile headers names a profiling build option", "[profile][boundary]") {
  const fs::path src{ATLANTIS_SOURCE_DIR};
  REQUIRE(fs::exists(src));
  constexpr std::string_view kForbidden[] = {"ATLANTIS_PROFILING", "ATLANTIS_GPU_MARKERS", "TRACY_ENABLE"};

  std::size_t scanned = 0;
  std::string violations;
  for (const auto& module : fs::directory_iterator(src)) {
    const fs::path include = module.path() / "include";
    if (!fs::exists(include)) continue;
    for (const auto& entry : fs::recursive_directory_iterator(include)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".h" || isProfileHeader(entry.path())) continue;
      ++scanned;
      const std::string text = readAll(entry.path());
      for (const std::string_view token : kForbidden) {
        if (text.find(token) != std::string::npos) {
          violations += entry.path().string() + ": " + std::string(token) + '\n';
        }
      }
    }
  }
  CHECK(scanned > 50);  // the scan really walked the public headers
  INFO(violations);
  CHECK(violations.empty());
}

TEST_CASE("only Core's profile.cpp and the Vulkan Backend's Tracy context file include a Tracy header",
          "[profile][boundary]") {
  const fs::path src{ATLANTIS_SOURCE_DIR};
  REQUIRE(fs::exists(src));
  const std::string allowedCore = (src / "core" / "src" / "profile.cpp").generic_string();
  const std::string allowedBackend = (src / "vulkan_backend" / "src" / "vulkan_tracy_context.cpp").generic_string();

  std::size_t scanned = 0;
  std::string violations;
  for (const auto& entry : fs::recursive_directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    const auto ext = entry.path().extension();
    if (ext != ".h" && ext != ".hpp" && ext != ".cpp") continue;
    ++scanned;
    const std::string generic = entry.path().generic_string();
    if (generic == allowedCore || generic == allowedBackend) continue;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      const auto hash = line.find('#');
      if (hash == std::string::npos || line.find("include", hash) == std::string::npos) continue;
      std::string lower = line;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (lower.find("tracy") != std::string::npos) violations += generic + ": " + line + '\n';
    }
  }
  CHECK(scanned > 200);
  INFO(violations);
  CHECK(violations.empty());
}

TEST_CASE("the Tracy target is loopback-only and on-demand", "[profile][boundary]") {
  // The listener's interface and the collection mode are compile definitions of
  // the atlantis_tracy target (ADR-0116 D2); a live check of the bound address is
  // part of the Plan 0059 M10 acceptance run.
  const std::string text = readAll(fs::path{ATLANTIS_CMAKE_DIR} / "AtlantisTracy.cmake");
  REQUIRE_FALSE(text.empty());
  CHECK(text.find("TRACY_ONLY_LOCALHOST") != std::string::npos);
  CHECK(text.find("TRACY_ON_DEMAND") != std::string::npos);
  CHECK(text.find("v0.14.1") != std::string::npos);
  CHECK(text.find("URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1") !=
        std::string::npos);
}

TEST_CASE("the profile headers include no Tracy and no Vulkan header", "[profile][boundary]") {
  const fs::path include = fs::path{ATLANTIS_SOURCE_DIR} / "core" / "include" / "atlantis";
  std::size_t scanned = 0;
  std::string violations;
  for (const auto& entry : fs::directory_iterator(include)) {
    if (!isProfileHeader(entry.path())) continue;
    ++scanned;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      const auto hash = line.find('#');
      if (hash == std::string::npos || line.find("include", hash) == std::string::npos) continue;
      std::string lower = line;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (lower.find("tracy") != std::string::npos || lower.find("vulkan") != std::string::npos) {
        violations += entry.path().string() + ": " + line + '\n';
      }
    }
  }
  CHECK(scanned == 3);
  INFO(violations);
  CHECK(violations.empty());
}

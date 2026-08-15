#include <filesystem>
#include <fstream>

#include <atlantis/shader_system/version_provenance.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::describeSdkProvenance;

TEST_CASE("describeSdkProvenance() finds a slang-standard-module-<version> sibling directory",
          "[shader_system][version_provenance]") {
  const auto root = std::filesystem::temp_directory_path() / "atlantis_version_provenance_test_present";
  std::filesystem::remove_all(root);
  const auto binDir = root / "1.4.357.0" / "Bin";
  std::filesystem::create_directories(binDir / "slang-standard-module-2026.13.1");
  std::ofstream(binDir / "slangc.exe") << "";  // content irrelevant -- only the path is used

  const auto result = describeSdkProvenance(binDir / "slangc.exe");
  REQUIRE(result.has_value());
  REQUIRE(*result == "1.4.357.0 / slang-standard-module-2026.13.1");

  std::filesystem::remove_all(root);
}

TEST_CASE("describeSdkProvenance() returns std::nullopt when no such directory exists",
          "[shader_system][version_provenance]") {
  const auto root = std::filesystem::temp_directory_path() / "atlantis_version_provenance_test_absent";
  std::filesystem::remove_all(root);
  const auto binDir = root / "1.4.357.0" / "Bin";
  std::filesystem::create_directories(binDir);
  std::ofstream(binDir / "slangc.exe") << "";

  const auto result = describeSdkProvenance(binDir / "slangc.exe");
  REQUIRE_FALSE(result.has_value());

  std::filesystem::remove_all(root);
}

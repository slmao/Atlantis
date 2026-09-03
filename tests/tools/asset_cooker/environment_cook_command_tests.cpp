#include <cook_command.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::runCookCommand;

TEST_CASE("environment cook mode fails cleanly for missing and malformed HDR sources", "[asset_cooker][environment]") {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "atlantis_environment_cook_command_tests";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root / "assets" / "environments");

  CookCommandRequest request;
  request.kind = AssetKind::Environment;
  request.assetRoot = (root / "assets").string();
  request.outputDir = (root / "out").string();
  request.stampPath = (root / "out" / "test_environment.stamp").string();
  request.sourcePath = (root / "assets" / "environments" / "missing.hdr").string();
  CHECK(runCookCommand(request) != 0);

  request.sourcePath = (root / "assets" / "environments" / "malformed.hdr").string();
  {
    std::ofstream malformed(request.sourcePath, std::ios::binary | std::ios::trunc);
    malformed << "not a Radiance HDR file\n";
  }
  CHECK(runCookCommand(request) != 0);
  CHECK_FALSE(std::filesystem::exists(root / "out" / "test_environment.aenv"));
  CHECK_FALSE(std::filesystem::exists(request.stampPath));
  std::filesystem::remove_all(root, error);
}

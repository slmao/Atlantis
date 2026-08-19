#pragma once

#include <string>

namespace atlantis::tools::asset_cooker {

// Plan 0012 Section D4: two modes. Cook mode (isValidateSet == false)
// cooks exactly one asset from sourcePath (relative to assetRoot) into
// outputDir, writing a completion stamp at stampPath on success --
// mirroring atlantis_shader_compiler's own stamp-based, all-or-nothing
// CMake integration (ADR-0029). Validate-set mode (isValidateSet ==
// true) reads assetListPath (one declared logical path per line),
// computes each one's real Asset ID via
// atlantis::asset_system::computeAssetId(), and runs
// atlantis::asset_system::validateAssetSet() over the resulting
// set -- scoped to exactly the declared paths in that one file, per
// ADR-0044.
struct CookCommandRequest {
  bool isValidateSet = false;

  // Cook mode.
  std::string sourcePath;
  std::string assetRoot;
  std::string outputDir;
  std::string stampPath;

  // Validate-set mode.
  std::string assetListPath;
};

// Returns the process exit code (0 success, non-zero failure). Kept
// separate from main() so tests can invoke it directly with a
// hand-built CookCommandRequest, without spawning a real process --
// the real cooker executable is still launched for real by
// cooker_determinism_tests.cpp's own "tool"-labeled test, which
// specifically needs a real subprocess.
[[nodiscard]] int runCookCommand(const CookCommandRequest& request);

}  // namespace atlantis::tools::asset_cooker

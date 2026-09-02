#pragma once

#include <string>

namespace atlantis::tools::asset_cooker {

// Plan 0015 Section D7 / Plan 0016 Section D9 / Plan 0018 Section P4:
// which cook-mode pipeline to run -- StaticMesh (the original, default
// behavior, unchanged), Scene, Texture, Material, or Environment. Never affects
// validate-set mode.
enum class AssetKind { StaticMesh, Scene, Texture, Material, Environment };

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
  AssetKind kind = AssetKind::StaticMesh;
  std::string sourcePath;
  std::string assetRoot;
  std::string outputDir;
  std::string stampPath;

  // Texture cook mode only (Plan 0016 Section D9) -- already validated
  // by main.cpp's own CLI parsing to be exactly "unorm" or "srgb" before
  // this request is ever built; kept as a raw string here (not
  // atlantis::asset_system::TextureColorSpace) so this header does not
  // need to depend on Asset System's own texture-specific types for one
  // field. runCookTextureMode() does the final string -> enum
  // conversion immediately before calling cookTexture().
  std::string colorSpace;

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

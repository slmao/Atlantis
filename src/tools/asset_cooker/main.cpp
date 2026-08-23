// Atlantis Tools: atlantis_asset_cooker CLI entry point. Cook mode is
// invoked once per declared asset by atlantis_add_static_mesh_asset();
// validate-set mode is invoked once by
// atlantis_finalize_asset_validation() (both in
// src/asset_system/CMakeLists.txt, Plan 0012 Section D4) via a plain
// --flag=value argv convention -- a Plan-stage mechanical detail, not
// an architectural surface.

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "cook_command.h"

namespace {

[[nodiscard]] std::optional<std::string> valueAfterEquals(std::string_view arg, std::string_view flag) {
  if (arg.substr(0, flag.size()) != flag) return std::nullopt;
  return std::string(arg.substr(flag.size()));
}

}  // namespace

int main(int argc, char** argv) {
  using atlantis::tools::asset_cooker::CookCommandRequest;
  using atlantis::tools::asset_cooker::runCookCommand;

  CookCommandRequest request;
  bool sawSource = false, sawAssetRoot = false, sawOutputDir = false, sawAssetList = false;
  bool sawUnrecognized = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--validate-set") {
      request.isValidateSet = true;
    } else if (auto source = valueAfterEquals(arg, "--source=")) {
      request.sourcePath = *source;
      sawSource = true;
    } else if (auto assetRoot = valueAfterEquals(arg, "--asset-root=")) {
      request.assetRoot = *assetRoot;
      sawAssetRoot = true;
    } else if (auto outputDir = valueAfterEquals(arg, "--output-dir=")) {
      request.outputDir = *outputDir;
      sawOutputDir = true;
    } else if (auto stamp = valueAfterEquals(arg, "--stamp=")) {
      request.stampPath = *stamp;
    } else if (auto assetList = valueAfterEquals(arg, "--asset-list=")) {
      request.assetListPath = *assetList;
      sawAssetList = true;
    } else if (auto kind = valueAfterEquals(arg, "--kind=")) {
      if (*kind == "mesh") {
        request.kind = atlantis::tools::asset_cooker::AssetKind::StaticMesh;
      } else if (*kind == "scene") {
        request.kind = atlantis::tools::asset_cooker::AssetKind::Scene;
      } else {
        std::cerr << "atlantis_asset_cooker: unrecognized --kind value: " << *kind << "\n";
        sawUnrecognized = true;
      }
    } else {
      std::cerr << "atlantis_asset_cooker: unrecognized argument: " << arg << "\n";
      sawUnrecognized = true;
    }
  }

  const bool haveRequiredFlags =
      request.isValidateSet ? sawAssetList : (sawSource && sawAssetRoot && sawOutputDir);

  if (sawUnrecognized || !haveRequiredFlags) {
    std::cerr << "usage: atlantis_asset_cooker [--kind=mesh|scene] --source=<path> --asset-root=<dir> "
                 "--output-dir=<dir> [--stamp=<path>]\n"
                 "       atlantis_asset_cooker --validate-set --asset-list=<path>\n";
    return 1;
  }

  // Last-line-of-defense exception safety net: every recoverable error
  // this module's own code returns is a Result<T, E>, never a thrown
  // exception (matching this project's render-path convention, even
  // though offline tooling is not strictly required to follow it -- see
  // AGENTS.md's own carve-out). Standard library operations this CLI
  // transitively calls (e.g. std::vector::reserve()) can still throw
  // std::bad_alloc under genuine resource exhaustion; without this,
  // that would propagate uncaught and terminate the process instead of
  // producing the stable, non-zero exit code and diagnostic message a
  // CLI tool's own caller (CMake's build graph) needs to fail cleanly.
  try {
    return runCookCommand(request);
  } catch (const std::exception& e) {
    std::cerr << "atlantis_asset_cooker: unexpected internal error: " << e.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "atlantis_asset_cooker: unexpected internal error (unknown exception type)\n";
    return 1;
  }
}

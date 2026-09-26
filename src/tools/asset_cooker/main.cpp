// Atlantis Tools: atlantis_asset_cooker CLI entry point. Cook mode is
// invoked once per declared asset by atlantis_add_static_mesh_asset();
// validate-set mode is invoked once by
// atlantis_finalize_asset_validation() (both in
// src/asset_system/CMakeLists.txt, Plan 0012 Section D4) via a plain
// --flag=value argv convention -- a Plan-stage mechanical detail, not
// an architectural surface.

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "cook_command.h"

int main(int argc, char** argv) {
  using atlantis::tools::asset_cooker::CookCommandRequest;
  using atlantis::tools::asset_cooker::parseCookArguments;
  using atlantis::tools::asset_cooker::runCookCommand;

  // Plan 0046 Milestone 2: the grammar lives in parseCookArguments()
  // (cook_command.cpp), shared with the cook-manifest mode.
  CookCommandRequest request;
  if (!parseCookArguments(std::vector<std::string>(argv + 1, argv + argc), request, std::cerr)) {
    std::cerr << "usage: atlantis_asset_cooker [--kind=mesh|scene|texture|material|environment] --source=<path> "
                 "--asset-root=<dir> "
                 "--output-dir=<dir> [--stamp=<path>] [--color-space=unorm|srgb]\n"
                 "       atlantis_asset_cooker --validate-set --asset-list=<path>\n"
                 "       atlantis_asset_cooker --kind=cook-manifest --import-dir=<dir> --cooked-dir=<dir> "
                 "--content-parent=<dir> --manifest-out=<path> [--stamp=<path>]\n";
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

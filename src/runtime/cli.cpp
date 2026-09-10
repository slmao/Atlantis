#include "cli.h"

#include <atlantis/assert.h>

namespace atlantis::runtime::cli {

namespace {

// Verbatim from Spec 0032's own already-approved "CLI surface" text
// (Proposed Design) -- both --help's own message and every error
// message's own trailing usage block reuse this one constant.
constexpr std::string_view kUsageText =
    "usage: atlantis_runtime [--scene <name>]\n"
    "       atlantis_runtime --list-scenes\n"
    "       atlantis_runtime --help\n"
    "\n"
    "<name> is one of: integrated_showcase_demo, ibl_material_demo, pbr_normal_map_demo\n";

[[nodiscard]] const SceneWhitelistEntry* findByName(std::span<const SceneWhitelistEntry> whitelist,
                                                      std::string_view name) {
  for (const auto& entry : whitelist) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}

// Every PrintErrorAndExit path shares one message shape: "atlantis_runtime: <diagnostic>\n\n<usage>".
[[nodiscard]] CommandLineResult makeError(std::string_view diagnostic) {
  CommandLineResult result;
  result.outcome = CommandLineOutcome::PrintErrorAndExit;
  result.message = std::string("atlantis_runtime: ").append(diagnostic).append("\n\n").append(kUsageText);
  return result;
}

}  // namespace

CommandLineResult parseCommandLine(int argc, char** argv, std::span<const SceneWhitelistEntry> whitelist) {
  int sceneCount = 0;
  int helpCount = 0;
  int listScenesCount = 0;
  std::string_view requestedSceneName;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--scene") {
      if (++sceneCount > 1) return makeError("--scene supplied more than once");
      if (i + 1 >= argc) return makeError("--scene requires a value");
      requestedSceneName = argv[++i];
    } else if (arg == "--help") {
      if (++helpCount > 1) return makeError("--help supplied more than once");
    } else if (arg == "--list-scenes") {
      if (++listScenesCount > 1) return makeError("--list-scenes supplied more than once");
    } else {
      // Covers an unknown flag, a bare positional argument, and the
      // explicitly-unsupported single-token "--scene=<name>" form
      // (Spec 0032 Non-Goals / Alternatives Considered) -- none of
      // these ever literally equal "--scene"/"--help"/"--list-scenes",
      // so all three fall into this one bucket by construction, with
      // no special-cased message.
      return makeError(std::string("unrecognized argument: ").append(arg));
    }
  }

  const int modesRequested = (sceneCount > 0 ? 1 : 0) + (helpCount > 0 ? 1 : 0) + (listScenesCount > 0 ? 1 : 0);
  if (modesRequested > 1) {
    return makeError("--help, --list-scenes, and --scene may not be combined");
  }

  if (helpCount > 0) {
    CommandLineResult result;
    result.outcome = CommandLineOutcome::PrintUsageAndExit;
    result.message = std::string(kUsageText);
    return result;
  }

  if (listScenesCount > 0) {
    CommandLineResult result;
    result.outcome = CommandLineOutcome::PrintUsageAndExit;
    std::string names;
    for (const auto& entry : whitelist) {
      names.append(entry.name).append("\n");
    }
    result.message = std::move(names);
    return result;
  }

  // Rows 16-17 of Plan 0032's own CLI behavior table: "--scene --help"/
  // "--scene --list-scenes" reach here with sceneCount == 1 and
  // requestedSceneName == "--help"/"--list-scenes" -- neither is a
  // whitelist entry name, so findByName() below naturally reports an
  // unrecognized --scene value; no special case needed.
  const std::string_view lookupName = sceneCount > 0 ? requestedSceneName : kDefaultSceneName;
  const SceneWhitelistEntry* matched = findByName(whitelist, lookupName);
  if (matched == nullptr) {
    if (sceneCount > 0) {
      return makeError(std::string("unrecognized --scene value: ").append(requestedSceneName));
    }
    // Caller-precondition violation (see cli.h's own kDefaultSceneName
    // comment): the injected whitelist is missing the default entry --
    // a programmer error, not user input. Matches exit_reason.cpp's
    // own ATLANTIS_CHECK_MSG + defensive-fallback-return pattern (the
    // installed assert handler is not guaranteed to terminate).
    ATLANTIS_CHECK_MSG(false, "parseCommandLine(): whitelist is missing the default scene entry");
    return makeError("internal error: default scene entry not found");
  }

  CommandLineResult result;
  result.outcome = CommandLineOutcome::RunScene;
  result.selectedScene = matched->paths;
  return result;
}

}  // namespace atlantis::runtime::cli

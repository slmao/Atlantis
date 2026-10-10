#include "cli.h"

#include <atlantis/assert.h>

namespace atlantis::runtime::cli {

namespace {

// Verbatim from Spec 0032's own already-approved "CLI surface" text
// (Proposed Design) -- both --help's own message and every error
// message's own trailing usage block reuse this one constant.
constexpr std::string_view kUsageText =
    "usage: atlantis_runtime [--scene <name>] [--exec <script|->] [--listen <port> [--session-file <path>]] "
    "[--editor]\n"
    "       atlantis_runtime --list-scenes\n"
    "       atlantis_runtime --help\n"
    "       atlantis_runtime --benchmark <scene> [--warmup <n>] [--frames <n>] [--resolution <W>x<H>]\n"
    "                        [--variant <v>] [--json] [--output <path>] [--baseline <path>] [--series]\n"
    "                        [--gpu-timing=on|off]\n"
    "\n"
    "<name> is one of: integrated_showcase_demo, ibl_material_demo, pbr_normal_map_demo, "
    "pbr_materials_showcase\n"
    "<v> is one of: baseline, no-point-lights, no-fog, no-bloom\n";

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

// Decimal digits only, in [minimum, maximum].
[[nodiscard]] bool parseUnsigned(std::string_view text, unsigned int minimum, unsigned int maximum,
                                  unsigned int& out) {
  if (text.empty() || text.size() > 9) return false;
  std::uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  if (value < minimum || value > maximum) return false;
  out = static_cast<unsigned int>(value);
  return true;
}

[[nodiscard]] std::optional<BenchmarkVariant> parseVariant(std::string_view text) {
  if (text == "baseline") return BenchmarkVariant::Baseline;
  if (text == "no-point-lights") return BenchmarkVariant::NoPointLights;
  if (text == "no-fog") return BenchmarkVariant::NoFog;
  if (text == "no-bloom") return BenchmarkVariant::NoBloom;
  return std::nullopt;
}

}  // namespace

std::string_view toString(BenchmarkVariant variant) noexcept {
  switch (variant) {
    case BenchmarkVariant::Baseline: return "baseline";
    case BenchmarkVariant::NoPointLights: return "no-point-lights";
    case BenchmarkVariant::NoFog: return "no-fog";
    case BenchmarkVariant::NoBloom: return "no-bloom";
  }
  return "baseline";
}

CommandLineResult parseCommandLine(int argc, char** argv, std::span<const SceneWhitelistEntry> whitelist) {
  int sceneCount = 0;
  int helpCount = 0;
  int listScenesCount = 0;
  int execCount = 0;
  int listenCount = 0;
  int sessionFileCount = 0;
  int editorCount = 0;
  std::string_view requestedSceneName;
  std::string_view execScript;
  std::string_view listenPort;
  std::string_view sessionFile;
  // Spec 0059 / ADR-0117 D1: --benchmark and its options.
  int benchmarkCount = 0, warmupCount = 0, framesCount = 0, resolutionCount = 0, variantCount = 0;
  int jsonCount = 0, outputCount = 0, baselineCount = 0, seriesCount = 0, gpuTimingCount = 0;
  std::string_view benchmarkScene, warmupText, framesText, resolutionText, variantText, outputPath, baselinePath;
  std::string_view gpuTimingText;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--scene") {
      if (++sceneCount > 1) return makeError("--scene supplied more than once");
      if (i + 1 >= argc) return makeError("--scene requires a value");
      requestedSceneName = argv[++i];
    } else if (arg == "--exec") {
      if (++execCount > 1) return makeError("--exec supplied more than once");
      if (i + 1 >= argc) return makeError("--exec requires a value");
      execScript = argv[++i];
    } else if (arg == "--listen") {
      if (++listenCount > 1) return makeError("--listen supplied more than once");
      if (i + 1 >= argc) return makeError("--listen requires a value");
      listenPort = argv[++i];
    } else if (arg == "--session-file") {
      if (++sessionFileCount > 1) return makeError("--session-file supplied more than once");
      if (i + 1 >= argc) return makeError("--session-file requires a value");
      sessionFile = argv[++i];
    } else if (arg == "--editor") {
      if (++editorCount > 1) return makeError("--editor supplied more than once");
    } else if (arg == "--benchmark") {
      if (++benchmarkCount > 1) return makeError("--benchmark supplied more than once");
      if (i + 1 >= argc) return makeError("--benchmark requires a scene name");
      benchmarkScene = argv[++i];
    } else if (arg == "--warmup") {
      if (++warmupCount > 1) return makeError("--warmup supplied more than once");
      if (i + 1 >= argc) return makeError("--warmup requires a value");
      warmupText = argv[++i];
    } else if (arg == "--frames") {
      if (++framesCount > 1) return makeError("--frames supplied more than once");
      if (i + 1 >= argc) return makeError("--frames requires a value");
      framesText = argv[++i];
    } else if (arg == "--resolution") {
      if (++resolutionCount > 1) return makeError("--resolution supplied more than once");
      if (i + 1 >= argc) return makeError("--resolution requires a value");
      resolutionText = argv[++i];
    } else if (arg == "--variant") {
      if (++variantCount > 1) return makeError("--variant supplied more than once");
      if (i + 1 >= argc) return makeError("--variant requires a value");
      variantText = argv[++i];
    } else if (arg == "--json") {
      if (++jsonCount > 1) return makeError("--json supplied more than once");
    } else if (arg == "--output") {
      if (++outputCount > 1) return makeError("--output supplied more than once");
      if (i + 1 >= argc) return makeError("--output requires a value");
      outputPath = argv[++i];
    } else if (arg == "--baseline") {
      if (++baselineCount > 1) return makeError("--baseline supplied more than once");
      if (i + 1 >= argc) return makeError("--baseline requires a value");
      baselinePath = argv[++i];
    } else if (arg == "--series") {
      if (++seriesCount > 1) return makeError("--series supplied more than once");
    } else if (arg.rfind("--gpu-timing=", 0) == 0) {
      if (++gpuTimingCount > 1) return makeError("--gpu-timing supplied more than once");
      gpuTimingText = arg.substr(std::string_view("--gpu-timing=").size());
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

  if (execCount > 0 && (helpCount > 0 || listScenesCount > 0)) {
    return makeError("--exec runs a scene; it may not be combined with --help or --list-scenes");
  }

  if ((listenCount > 0 || sessionFileCount > 0) && (helpCount > 0 || listScenesCount > 0)) {
    return makeError("--listen runs a scene; it may not be combined with --help or --list-scenes");
  }
  if (sessionFileCount > 0 && listenCount == 0) return makeError("--session-file requires --listen");
  // Plan 0056 P10 (J9): the editor and an --exec script would both drive the
  // frame.
  if (editorCount > 0 && (helpCount > 0 || listScenesCount > 0)) {
    return makeError("--editor runs a scene; it may not be combined with --help or --list-scenes");
  }
  if (editorCount > 0 && execCount > 0) return makeError("--editor and --exec may not be combined");
  // Spec 0059 / ADR-0117 D1: the benchmark measures a scene; nothing else may drive it.
  const int benchmarkOptions = warmupCount + framesCount + resolutionCount + variantCount + jsonCount + outputCount +
                               baselineCount + seriesCount + gpuTimingCount;
  if (benchmarkCount == 0 && benchmarkOptions > 0) {
    return makeError("--warmup, --frames, --resolution, --variant, --json, --output, --baseline, --series and "
                     "--gpu-timing require --benchmark");
  }
  if (benchmarkCount > 0) {
    if (sceneCount > 0 || execCount > 0 || listenCount > 0 || sessionFileCount > 0 || editorCount > 0 ||
        helpCount > 0 || listScenesCount > 0) {
      return makeError("--benchmark may not be combined with --scene, --exec, --listen, --session-file, --editor, "
                       "--help or --list-scenes");
    }
    BenchmarkRequest request;
    request.sceneName = std::string(benchmarkScene);
    if (const SceneWhitelistEntry* entry = findByName(whitelist, benchmarkScene)) request.scene = entry->selection;
    if (warmupCount > 0 && !parseUnsigned(warmupText, 0, 1000000, request.warmup)) {
      return makeError(std::string("invalid --warmup: ").append(warmupText));
    }
    if (framesCount > 0 && !parseUnsigned(framesText, 1, 1000000, request.frames)) {
      return makeError(std::string("invalid --frames: ").append(framesText));
    }
    if (resolutionCount > 0) {
      const auto x = resolutionText.find('x');
      if (x == std::string_view::npos || !parseUnsigned(resolutionText.substr(0, x), 1, 16384, request.width) ||
          !parseUnsigned(resolutionText.substr(x + 1), 1, 16384, request.height)) {
        return makeError(std::string("invalid --resolution (expected <W>x<H>): ").append(resolutionText));
      }
    }
    if (variantCount > 0) {
      const auto variant = parseVariant(variantText);
      if (!variant.has_value()) return makeError(std::string("unrecognized --variant value: ").append(variantText));
      request.variant = *variant;
    }
    request.json = jsonCount > 0;
    if (outputCount > 0) request.outputPath = std::string(outputPath);
    if (baselineCount > 0) request.baselinePath = std::string(baselinePath);
    request.series = seriesCount > 0;
    if (gpuTimingCount > 0) {
      if (gpuTimingText == "on") {
        request.gpuTiming = true;
      } else if (gpuTimingText == "off") {
        request.gpuTiming = false;
      } else {
        return makeError(std::string("invalid --gpu-timing value (on or off): ").append(gpuTimingText));
      }
    }
    CommandLineResult result;
    result.outcome = CommandLineOutcome::RunScene;
    result.benchmark = std::move(request);
    return result;
  }

  std::optional<std::uint16_t> port;
  if (listenCount > 0) {
    // Decimal digits only, 0-65535 (0: an ephemeral port).
    std::uint32_t value = 0;
    bool valid = !listenPort.empty() && listenPort.size() <= 5;
    for (const char c : listenPort) {
      if (c < '0' || c > '9') valid = false;
      if (valid) value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (!valid || value > 65535) return makeError(std::string("invalid --listen port: ").append(listenPort));
    port = static_cast<std::uint16_t>(value);
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
  result.selectedScene = matched->selection;
  if (execCount > 0) result.execScript = std::string(execScript);
  result.listenPort = port;
  if (sessionFileCount > 0) result.sessionFile = std::string(sessionFile);
  result.editor = editorCount > 0;
  return result;
}

}  // namespace atlantis::runtime::cli

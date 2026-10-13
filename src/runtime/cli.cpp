#include "cli.h"

#include <atlantis/assert.h>

#include <charconv>

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
    "                        [--gpu-timing=on|off] [--warmup-seconds <s>] [--pin-process on|off]\n"
    "       atlantis_runtime --gate [--scene <name>] [--variant <v>] [--resolution <W>x<H>] [--runs <n>]\n"
    "                        [--baselines <dir>] [--profile <id>|auto] [--policy <file>] [--json]\n"
    "                        [--output <path>] [--idle-wait on|off] [--preheat-seconds <s>] [--pin-process on|off]\n"
    "                        [--require-mains on|off]\n"
    "       atlantis_runtime --gate --record --record-to <dir> --reason-category <1-4> --reason <text>\n"
    "                        [--sessions <n>] [--session-gap-minutes <m>] [the --gate options above]\n"
    "       atlantis_runtime --gate --applies [--applicability <file>]   (changed paths on stdin)\n"
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

// A decimal number with an optional fraction ("90", "2.5"), in [minimum, maximum].
[[nodiscard]] bool parseDecimal(std::string_view text, double minimum, double maximum, double& out) {
  if (text.empty() || text.size() > 12) return false;
  std::size_t dots = 0;
  for (const char c : text) {
    if (c == '.') {
      ++dots;
    } else if (c < '0' || c > '9') {
      return false;
    }
  }
  if (dots > 1 || text.front() == '.' || text.back() == '.') return false;
  double value = 0.0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return false;
  if (value < minimum || value > maximum) return false;
  out = value;
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
  // Plan 0060 M3 (ADR-0119 D5): --gate and its options; --warmup-seconds is the benchmark's.
  int gateCount = 0, recordCount = 0, appliesCount = 0, runsCount = 0, baselinesCount = 0, profileCount = 0;
  int policyCount = 0, idleWaitCount = 0, preheatCount = 0, sessionsCount = 0, gapCount = 0, categoryCount = 0;
  int reasonCount = 0, recordToCount = 0, applicabilityCount = 0, warmupSecondsCount = 0, pinCount = 0;
  int requireMainsCount = 0;
  std::string_view pinText, requireMainsText;
  std::string_view runsText, baselinesDir, profileId, policyPath, idleWaitText, preheatText, sessionsText, gapText;
  std::string_view categoryText, reasonText, recordTo, applicabilityPath, warmupSecondsText;

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
    } else if (arg == "--warmup-seconds") {
      if (++warmupSecondsCount > 1) return makeError("--warmup-seconds supplied more than once");
      if (i + 1 >= argc) return makeError("--warmup-seconds requires a value");
      warmupSecondsText = argv[++i];
    } else if (arg == "--pin-process") {
      if (++pinCount > 1) return makeError("--pin-process supplied more than once");
      if (i + 1 >= argc) return makeError("--pin-process requires a value");
      pinText = argv[++i];
    } else if (arg == "--require-mains") {
      if (++requireMainsCount > 1) return makeError("--require-mains supplied more than once");
      if (i + 1 >= argc) return makeError("--require-mains requires a value");
      requireMainsText = argv[++i];
    } else if (arg == "--gate") {
      if (++gateCount > 1) return makeError("--gate supplied more than once");
    } else if (arg == "--record") {
      if (++recordCount > 1) return makeError("--record supplied more than once");
    } else if (arg == "--applies") {
      if (++appliesCount > 1) return makeError("--applies supplied more than once");
    } else if (arg == "--runs") {
      if (++runsCount > 1) return makeError("--runs supplied more than once");
      if (i + 1 >= argc) return makeError("--runs requires a value");
      runsText = argv[++i];
    } else if (arg == "--baselines") {
      if (++baselinesCount > 1) return makeError("--baselines supplied more than once");
      if (i + 1 >= argc) return makeError("--baselines requires a value");
      baselinesDir = argv[++i];
    } else if (arg == "--profile") {
      if (++profileCount > 1) return makeError("--profile supplied more than once");
      if (i + 1 >= argc) return makeError("--profile requires a value");
      profileId = argv[++i];
    } else if (arg == "--policy") {
      if (++policyCount > 1) return makeError("--policy supplied more than once");
      if (i + 1 >= argc) return makeError("--policy requires a value");
      policyPath = argv[++i];
    } else if (arg == "--idle-wait") {
      if (++idleWaitCount > 1) return makeError("--idle-wait supplied more than once");
      if (i + 1 >= argc) return makeError("--idle-wait requires a value");
      idleWaitText = argv[++i];
    } else if (arg == "--preheat-seconds") {
      if (++preheatCount > 1) return makeError("--preheat-seconds supplied more than once");
      if (i + 1 >= argc) return makeError("--preheat-seconds requires a value");
      preheatText = argv[++i];
    } else if (arg == "--sessions") {
      if (++sessionsCount > 1) return makeError("--sessions supplied more than once");
      if (i + 1 >= argc) return makeError("--sessions requires a value");
      sessionsText = argv[++i];
    } else if (arg == "--session-gap-minutes") {
      if (++gapCount > 1) return makeError("--session-gap-minutes supplied more than once");
      if (i + 1 >= argc) return makeError("--session-gap-minutes requires a value");
      gapText = argv[++i];
    } else if (arg == "--reason-category") {
      if (++categoryCount > 1) return makeError("--reason-category supplied more than once");
      if (i + 1 >= argc) return makeError("--reason-category requires a value");
      categoryText = argv[++i];
    } else if (arg == "--reason") {
      if (++reasonCount > 1) return makeError("--reason supplied more than once");
      if (i + 1 >= argc) return makeError("--reason requires a value");
      reasonText = argv[++i];
    } else if (arg == "--record-to") {
      if (++recordToCount > 1) return makeError("--record-to supplied more than once");
      if (i + 1 >= argc) return makeError("--record-to requires a value");
      recordTo = argv[++i];
    } else if (arg == "--applicability") {
      if (++applicabilityCount > 1) return makeError("--applicability supplied more than once");
      if (i + 1 >= argc) return makeError("--applicability requires a value");
      applicabilityPath = argv[++i];
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
                               baselineCount + seriesCount + gpuTimingCount + warmupSecondsCount + pinCount;
  // Plan 0060 M3: --variant, --resolution, --json and --output are shared with --gate; the rest are the
  // benchmark's alone.
  const int benchmarkOnlyOptions = warmupCount + framesCount + baselineCount + seriesCount + gpuTimingCount +
                                   warmupSecondsCount;
  const int gateOnlyOptions = runsCount + baselinesCount + profileCount + policyCount + idleWaitCount + preheatCount +
                              recordCount + appliesCount + sessionsCount + gapCount + categoryCount + reasonCount +
                              recordToCount + applicabilityCount + requireMainsCount;
  if (benchmarkCount == 0 && gateCount == 0 && benchmarkOptions > 0) {
    return makeError("--warmup, --frames, --resolution, --variant, --json, --output, --baseline, --series and "
                     "--gpu-timing require --benchmark");
  }
  if (gateCount == 0 && gateOnlyOptions > 0) {
    return makeError("--runs, --baselines, --profile, --policy, --idle-wait, --preheat-seconds, --record, --applies, "
                     "--sessions, --session-gap-minutes, --reason-category, --reason, --record-to, --applicability and "
                     "--require-mains require --gate");
  }
  if (gateCount > 0) {
    if (benchmarkCount > 0 || execCount > 0 || listenCount > 0 || sessionFileCount > 0 || editorCount > 0 ||
        helpCount > 0 || listScenesCount > 0) {
      return makeError("--gate may not be combined with --benchmark, --exec, --listen, --session-file, --editor, "
                       "--help or --list-scenes");
    }
    if (benchmarkOnlyOptions > 0) {
      return makeError("--warmup, --frames, --baseline, --series, --gpu-timing and --warmup-seconds require "
                       "--benchmark");
    }
    GateRequest request;
    if (sceneCount > 0) request.sceneName = std::string(requestedSceneName);
    if (const SceneWhitelistEntry* entry = findByName(whitelist, request.sceneName)) request.scene = entry->selection;
    if (recordCount > 0 && appliesCount > 0) return makeError("--record and --applies may not be combined");
    request.mode = recordCount > 0 ? GateRequest::Mode::Record
                                   : (appliesCount > 0 ? GateRequest::Mode::Applies : GateRequest::Mode::Gate);
    if (request.mode == GateRequest::Mode::Applies) {
      if (sceneCount + variantCount + resolutionCount + runsCount + baselinesCount + profileCount + policyCount +
              idleWaitCount + preheatCount + pinCount + requireMainsCount + jsonCount + outputCount + sessionsCount +
              gapCount + categoryCount + reasonCount + recordToCount >
          0) {
        return makeError("--applies takes only --applicability");
      }
    } else if (applicabilityCount > 0) {
      return makeError("--applicability requires --applies");
    }
    if (request.mode != GateRequest::Mode::Record &&
        sessionsCount + gapCount + categoryCount + reasonCount + recordToCount > 0) {
      return makeError("--sessions, --session-gap-minutes, --reason-category, --reason and --record-to require "
                       "--record");
    }
    if (variantCount > 0) {
      const auto variant = parseVariant(variantText);
      if (!variant.has_value()) return makeError(std::string("unrecognized --variant value: ").append(variantText));
      request.variant = *variant;
    }
    if (resolutionCount > 0) {
      const auto x = resolutionText.find('x');
      if (x == std::string_view::npos || !parseUnsigned(resolutionText.substr(0, x), 1, 16384, request.width) ||
          !parseUnsigned(resolutionText.substr(x + 1), 1, 16384, request.height)) {
        return makeError(std::string("invalid --resolution (expected <W>x<H>): ").append(resolutionText));
      }
    }
    if (runsCount > 0) {
      unsigned int runs = 0;
      if (!parseUnsigned(runsText, 1, 100, runs)) return makeError(std::string("invalid --runs: ").append(runsText));
      request.runs = runs;
    }
    if (preheatCount > 0) {
      double seconds = 0.0;
      if (!parseDecimal(preheatText, 0.0, 3600.0, seconds)) {
        return makeError(std::string("invalid --preheat-seconds: ").append(preheatText));
      }
      request.preheatSeconds = seconds;
    }
    if (idleWaitCount > 0) {
      if (idleWaitText == "on") {
        request.idleWait = true;
      } else if (idleWaitText == "off") {
        request.idleWait = false;
      } else {
        return makeError(std::string("invalid --idle-wait value (on or off): ").append(idleWaitText));
      }
    }
    if (pinCount > 0) {
      if (pinText == "on") {
        request.pinProcess = true;
      } else if (pinText == "off") {
        request.pinProcess = false;
      } else {
        return makeError(std::string("invalid --pin-process value (on or off): ").append(pinText));
      }
    }
    if (requireMainsCount > 0) {
      if (requireMainsText == "on") {
        request.requireMains = true;
      } else if (requireMainsText == "off") {
        request.requireMains = false;
      } else {
        return makeError(std::string("invalid --require-mains value (on or off): ").append(requireMainsText));
      }
    }
    if (baselinesCount > 0) request.baselinesDir = std::string(baselinesDir);
    if (profileCount > 0) request.profileId = std::string(profileId);
    if (policyCount > 0) request.policyPath = std::string(policyPath);
    request.json = jsonCount > 0;
    if (outputCount > 0) request.outputPath = std::string(outputPath);
    if (applicabilityCount > 0) request.applicabilityPath = std::string(applicabilityPath);
    if (request.mode == GateRequest::Mode::Record) {
      if (recordToCount == 0 || categoryCount == 0 || reasonCount == 0) {
        return makeError("--record requires --record-to, --reason-category and --reason (a baseline is only ever "
                         "recorded with a stated reason)");
      }
      if (!parseUnsigned(categoryText, 1, 4, request.reasonCategory)) {
        return makeError(std::string("invalid --reason-category (1 to 4): ").append(categoryText));
      }
      if (reasonText.empty() || reasonText.size() > 2000) return makeError("--reason must be 1 to 2000 characters");
      request.reasonText = std::string(reasonText);
      request.recordTo = std::string(recordTo);
      if (sessionsCount > 0 && !parseUnsigned(sessionsText, 1, 20, request.sessions)) {
        return makeError(std::string("invalid --sessions: ").append(sessionsText));
      }
      if (gapCount > 0 && !parseDecimal(gapText, 0.0, 1440.0, request.sessionGapMinutes)) {
        return makeError(std::string("invalid --session-gap-minutes: ").append(gapText));
      }
    }
    CommandLineResult result;
    result.outcome = CommandLineOutcome::RunScene;
    result.gate = std::move(request);
    return result;
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
    if (pinCount > 0) {
      if (pinText == "on") {
        request.pinProcess = true;
      } else if (pinText == "off") {
        request.pinProcess = false;
      } else {
        return makeError(std::string("invalid --pin-process value (on or off): ").append(pinText));
      }
    }
    if (warmupSecondsCount > 0 && !parseDecimal(warmupSecondsText, 0.0, 3600.0, request.warmupSeconds)) {
      return makeError(std::string("invalid --warmup-seconds: ").append(warmupSecondsText));
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

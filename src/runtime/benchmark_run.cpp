#include "benchmark_run.h"

#include "benchmark_loop.h"
#include "benchmark_report.h"
#include "benchmark_variants.h"
#include "gate_orchestrate.h"

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/json.h>
#include <atlantis/log.h>
#include <atlantis/profile_recorder.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/runtime_application.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace atlantis::runtime::benchmark {

namespace json = atlantis::connection::json;
namespace profile = atlantis::profile;

namespace {

// Everything goes to stderr while a benchmark runs: the default sink writes Info to
// stdout, which would corrupt the one JSON document that stdout is reserved for.
class StderrLogSink final : public atlantis::LogSink {
 public:
  void write(atlantis::LogLevel level, std::string_view message) override {
    std::cerr << '[' << atlantis::toString(level) << "] " << message << '\n';
  }
};

[[nodiscard]] std::string cpuBrand() {
#if defined(_MSC_VER)
  int regs[4] = {0, 0, 0, 0};
  __cpuid(regs, static_cast<int>(0x80000000));
  if (static_cast<unsigned int>(regs[0]) < 0x80000004u) return {};
  char brand[49] = {};
  for (int i = 0; i < 3; ++i) {
    __cpuid(regs, static_cast<int>(0x80000002u + static_cast<unsigned int>(i)));
    std::memcpy(brand + 16 * i, regs, 16);
  }
  std::string text(brand);
  const auto first = text.find_first_not_of(' ');
  return first == std::string::npos ? std::string() : text.substr(first);
#else
  return {};
#endif
}

#ifndef ATLANTIS_BUILD_TYPE
#define ATLANTIS_BUILD_TYPE "unknown"
#endif
#ifndef ATLANTIS_GIT_REVISION
#define ATLANTIS_GIT_REVISION ""
#endif
#ifndef ATLANTIS_GIT_DIRTY
#define ATLANTIS_GIT_DIRTY 0
#endif
#ifndef ATLANTIS_BISTRO_CONTENT_PIN
#define ATLANTIS_BISTRO_CONTENT_PIN ""
#endif

[[nodiscard]] BuildInfo buildInfo() {
  BuildInfo info;
  info.type = ATLANTIS_BUILD_TYPE;
#if defined(_MSC_VER)
  info.compiler = "MSVC " + std::to_string(_MSC_VER);
#else
  info.compiler = "unknown";
#endif
  info.gitRevision = ATLANTIS_GIT_REVISION;
  info.gitDirty = ATLANTIS_GIT_DIRTY != 0;
#if defined(ATLANTIS_PROFILING)
  info.profiling = true;
#endif
#if defined(ATLANTIS_PROFILING_TRACY)
  info.tracy = true;
#endif
#if defined(ATLANTIS_GPU_MARKERS)
  info.gpuMarkers = true;
#endif
  // Validation layers are forced on in a Debug build (AGENTS.md) and requested
  // off for a benchmark in every other configuration.
  info.validation = info.type == "Debug";
  return info;
}

[[nodiscard]] std::optional<std::string> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << content;
  return static_cast<bool>(out);
}

}  // namespace

BuildInfo currentBuildInfo() { return buildInfo(); }
std::string currentCpuBrand() { return cpuBrand(); }

StderrLogScope::StderrLogScope() { atlantis::log::initialize(std::make_shared<StderrLogSink>()); }
StderrLogScope::~StderrLogScope() { atlantis::log::initialize(nullptr); }

int runBenchmark(const cli::BenchmarkRequest& request, atlantis::runtime::BootstrapConfig config) {
#if !defined(ATLANTIS_PROFILING)
  static_cast<void>(request);
  static_cast<void>(config);
  std::cerr << "atlantis_runtime: --benchmark needs a build with ATLANTIS_PROFILING=ON (this build has none); "
               "configure with -DATLANTIS_PROFILING=ON\n";
  return kExitEnvironment;
#else
  const BuildInfo build = buildInfo();
  if (build.type == "Debug") {
    std::cerr << "atlantis_runtime: warning: a Debug build (validation layers on) is not a meaningful benchmark; "
                 "the result says so in meta.build\n";
  }

  // Owner ruling on the Plan 0060 M7 stop: the gate's children run pinned (no power throttling, high
  // priority). A run that was asked to be pinned and could not be refuses rather than pose as one.
  if (request.pinProcess) {
    std::string pinError;
    if (!gate::pinCurrentProcess(pinError)) {
      std::cerr << "atlantis_runtime: --benchmark --pin-process on: " << pinError << "\n";
      return kExitEnvironment;
    }
  }

  profile::setGpuTimingEnabled(request.gpuTiming);
  config.requestedClientWidth = request.width;
  config.requestedClientHeight = request.height;
  config.enableValidationLayers = false;  // a Debug build still forces them on

  auto created = createRuntimeApplication(config);
  if (created.isErr()) {
    std::cerr << "atlantis_runtime: --benchmark: " << toString(created.error()) << "\n";
    return kExitInitFailure;
  }
  RuntimeApplication app = std::move(created.value());
  auto connection = app.openConnection();

  // The variant is submitted now and applied at the start of the first frame.
  if (const auto edit = submitVariant(*connection, request.variant); edit.isErr()) {
    std::cerr << "atlantis_runtime: --benchmark --variant " << cli::toString(request.variant) << ": " << edit.error()
              << "\n";
    static_cast<void>(app.shutdown());
    return kExitRuntimeFailure;
  }

  int failure = kExitOk;
  bool firstFrame = true;
  bool extentChecked = false;
  std::optional<atlantis::rhi::Extent2D> actualExtent;

  LoopHooks hooks;
  hooks.now = [] { return std::chrono::steady_clock::now(); };
  hooks.runFrame = [&]() -> bool {
    if (!app.shouldContinue()) {
      failure = kExitRuntimeFailure;
      return false;
    }
    app.runFrame();
    if (firstFrame) {
      firstFrame = false;
      if (const std::string problem = verifyVariant(*connection, request.variant); !problem.empty()) {
        std::cerr << "atlantis_runtime: --benchmark: the variant did not take: " << problem << "\n";
        failure = kExitRuntimeFailure;
        return false;
      }
    }
    if (!extentChecked) {
      if (const auto extent = app.presentationExtent(); extent.has_value()) {
        extentChecked = true;
        actualExtent = extent;
        if (extent->width != request.width || extent->height != request.height) {
          std::cerr << "atlantis_runtime: --benchmark: the scene renders at " << extent->width << "x" << extent->height
                    << ", not the requested " << request.width << "x" << request.height
                    << " (the window could not be created at that size)\n";
          failure = kExitEnvironment;
          return false;
        }
      }
    }
    return app.shouldContinue();
  };

  LoopOptions options;
  options.warmup = request.warmup;
  options.warmupDuration = std::chrono::milliseconds(static_cast<std::int64_t>(request.warmupSeconds * 1000.0));
  options.frames = request.frames;
  const LoopResult loop = runMeasuredLoop(options, hooks);

  const RuntimeExitReason reason = app.shutdown();
  if (failure != kExitOk) return failure;
  if (loop.status == LoopStatus::NoPresentedFrames) {
    std::cerr << "atlantis_runtime: --benchmark: no frame was presented for 15 seconds (is the window minimized or "
                 "hidden?)\n";
    return kExitRuntimeFailure;
  }
  if (loop.status != LoopStatus::Ok || reason != RuntimeExitReason::Success) {
    std::cerr << "atlantis_runtime: --benchmark: the Runtime failed during the run\n";
    return kExitRuntimeFailure;
  }

  ReportInput input;
  input.build = build;
  input.system.os = "windows";
  input.system.cpu = cpuBrand();
  input.device = profile::deviceInfo();
  input.scene.name = request.sceneName;
  input.scene.guid = atlantis::asset_system::toString(config.sceneAsset);
  input.scene.contentPin = request.sceneName == "bistro" ? ATLANTIS_BISTRO_CONTENT_PIN : "";
  input.settings.width = actualExtent ? actualExtent->width : request.width;
  input.settings.height = actualExtent ? actualExtent->height : request.height;
  input.settings.warmup = request.warmup;
  input.settings.frames = request.frames;
  input.settings.variant = std::string(cli::toString(request.variant));
  input.settings.gpuTiming = request.gpuTiming;
  input.includeSeries = request.series;
  input.discardedFrames = loop.warmupRecords;
  input.skippedFrames = loop.skippedMeasured;
  for (std::size_t i = loop.warmupRecords; i < loop.warmupRecords + loop.measuredRecords; ++i) {
    if (!loop.records[i].presented) continue;
    MeasuredFrame frame;
    frame.record = loop.records[i];
    frame.frameMs = static_cast<double>(loop.records[i + 1].startNs - loop.records[i].startNs) / 1.0e6;
    input.frames.push_back(std::move(frame));
  }

  json::Value document = buildDocument(input);
  if (request.baselinePath.has_value()) {
    json::Value comparison;
    const auto text = readFile(*request.baselinePath);
    const auto baseline = text ? json::parse(*text) : atlantis::Result<json::Value, json::ParseError>::Err({0, "unreadable"});
    if (baseline.isOk()) {
      comparison = compareDocuments(baseline.value(), document);
    } else {
      std::cerr << "atlantis_runtime: --benchmark: cannot read the baseline " << *request.baselinePath << "\n";
      comparison = json::Value::object();
      comparison.set("comparable", json::Value::boolean(false));
      json::Value reasons = json::Value::array();
      reasons.push(json::Value::string("the baseline file could not be read or is not a benchmark document"));
      comparison.set("reasons", std::move(reasons));
    }
    document.set("comparison", std::move(comparison));
  }

  const std::string serialized = json::write(document);
  if (request.outputPath.has_value() && !writeFile(*request.outputPath, serialized + "\n")) {
    std::cerr << "atlantis_runtime: --benchmark: cannot write " << *request.outputPath << "\n";
    return kExitInitFailure;
  }
  if (request.json) {
    std::cout << serialized << "\n";
  } else {
    std::cout << summarize(document);
  }
  return kExitOk;
#endif
}

}  // namespace atlantis::runtime::benchmark

#include "gate_orchestrate.h"

#include <atlantis/connection/json.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <thread>

namespace atlantis::runtime::gate {

namespace json = atlantis::connection::json;
namespace fs = std::filesystem;

namespace {

class SystemClock final : public Clock {
 public:
  double nowSeconds() override {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void sleepSeconds(double seconds) override { std::this_thread::sleep_for(std::chrono::duration<double>(seconds)); }
  std::string isoUtc() override {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
  }
};

// A child's outcome as the exit code the gate returns without a verdict, or 0 for a clean run.
struct Mapped {
  int exitCode = 0;
  std::string message;
};

[[nodiscard]] Mapped mapChild(const ChildResult& result, std::string_view what) {
  Mapped out;
  switch (result.status) {
    case ChildResult::Status::LaunchFailed:
      out.exitCode = 1;
      out.message = std::string(what) + ": could not start the benchmark process: " + result.message;
      return out;
    case ChildResult::Status::TimedOut:
      out.exitCode = 2;
      out.message = std::string(what) + ": the benchmark process did not finish in time (" + result.message + ")";
      return out;
    case ChildResult::Status::Exited:
      break;
  }
  if (result.exitCode == 0) return out;
  // The child's own classification (ADR-0117 / Plan 0059 J7) carries over: an environment problem
  // stays one, a usage or initialization failure stays one, anything else is a runtime failure.
  out.exitCode = result.exitCode == 3 ? 3 : (result.exitCode == 1 ? 1 : 2);
  out.message = std::string(what) + ": the benchmark process exited with code " + std::to_string(result.exitCode);
  return out;
}

[[nodiscard]] std::optional<Value> readDocument(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto parsed = json::parse(text);
  if (parsed.isErr()) return std::nullopt;
  return std::move(parsed.value());
}

void removeQuietly(const std::string& path) {
  std::error_code ignored;
  fs::remove(fs::path(path), ignored);
}

}  // namespace

std::unique_ptr<Clock> makeSystemClock() { return std::make_unique<SystemClock>(); }

IdleResult waitForIdle(const IdlePolicy& policy, CpuSampler& sampler, Clock& clock) {
  const auto first = sampler.read();
  if (!first) return IdleResult::Unavailable;
  CpuTimes previous = *first;
  const double start = clock.nowSeconds();
  double quietSince = start;
  for (;;) {
    clock.sleepSeconds(1.0);
    const double now = clock.nowSeconds();
    const auto current = sampler.read();
    if (!current) return IdleResult::Unavailable;
    const std::uint64_t total = current->total - previous.total;
    const std::uint64_t idle = current->idle - previous.idle;
    previous = *current;
    // An interval with no elapsed CPU time says nothing; it does not break the window.
    const double idlePct = total > 0 ? 100.0 * static_cast<double>(idle) / static_cast<double>(total) : 100.0;
    if (idlePct < policy.cpuIdlePct) quietSince = now;
    if (now - quietSince >= policy.windowSeconds) return IdleResult::Idle;
    if (now - start >= policy.timeoutSeconds) return IdleResult::TimedOut;
  }
}

std::vector<std::string> childArguments(const RunPlan& plan, const std::string& outputPath, double warmupSeconds) {
  std::vector<std::string> args = {"--benchmark",
                                   plan.scene,
                                   "--variant",
                                   std::string(atlantis::runtime::cli::toString(plan.variant)),
                                   "--resolution",
                                   std::to_string(plan.width) + "x" + std::to_string(plan.height),
                                   "--output",
                                   outputPath};
  if (plan.pin) {
    args.push_back("--pin-process");
    args.push_back("on");
  }
  if (warmupSeconds > 0.0) {
    args.push_back("--warmup-seconds");
    args.push_back(formatNumber(warmupSeconds));
    args.push_back("--frames");
    args.push_back("1");
  }
  return args;
}

std::vector<std::string> canaryArguments(const RunPlan& plan, const std::string& outputPath) {
  std::vector<std::string> args = {"--canary", "--output", outputPath};
  if (plan.pin) {
    args.push_back("--pin-process");
    args.push_back("on");
  }
  return args;
}

namespace {

// One canary child. False, with `out` carrying the exit code and the reason, when it fails.
[[nodiscard]] bool measureCanary(const RunPlan& plan, ChildLauncher& launcher, const std::string& phase,
                                 SessionResult& out) {
  const std::string what = "the GPU canary (" + phase + ")";
  const std::string output = (fs::path(plan.tempDir) / ("canary_" + phase + ".json")).string();
  ChildSpec spec;
  spec.args = canaryArguments(plan, output);
  spec.timeoutSeconds = plan.childTimeoutSeconds;
  const ChildResult result = launcher.run(spec);
  if (const Mapped mapped = mapChild(result, what); mapped.exitCode != 0) {
    removeQuietly(output);
    out.exitCode = mapped.exitCode;
    out.message = mapped.message;
    return false;
  }
  std::optional<Value> document = readDocument(output);
  removeQuietly(output);
  if (!document) {
    out.exitCode = 2;
    out.message = what + ": the canary process wrote no valid document";
    return false;
  }
  out.canaryDocuments.emplace_back(phase, std::move(*document));
  return true;
}

}  // namespace

SessionResult runSession(const RunPlan& plan, ChildLauncher& launcher, CpuSampler& sampler, Clock& clock) {
  SessionResult out;

  if (plan.idleWait) {
    switch (waitForIdle(plan.idle, sampler, clock)) {
      case IdleResult::Idle:
        break;
      case IdleResult::TimedOut:
        out.exitCode = 3;
        out.message = "the machine did not become idle within " + formatNumber(plan.idle.timeoutSeconds) +
                      " s (system CPU use stayed above " + formatNumber(100.0 - plan.idle.cpuIdlePct) +
                      " %): wait for background work to finish, then run the gate again";
        return out;
      case IdleResult::Unavailable:
        out.exitCode = 3;
        out.message = "cannot read the system CPU times to check that the machine is idle; --idle-wait off skips "
                      "the check (the run is then marked non-conformant)";
        return out;
    }
  }

  if (plan.canary) {
    if (!measureCanary(plan, launcher, "start", out)) return out;
    if (plan.startCheck) {
      if (const std::optional<std::string> refusal = plan.startCheck(out.canaryDocuments.back().second)) {
        out.exitCode = 3;
        out.message = *refusal;
        return out;
      }
    }
  }

  if (plan.preheatSeconds > 0.0) {
    const std::string output = (fs::path(plan.tempDir) / "preheat.json").string();
    ChildSpec spec;
    spec.args = childArguments(plan, output, plan.preheatSeconds);
    spec.timeoutSeconds = plan.childTimeoutSeconds + plan.preheatSeconds;
    const ChildResult result = launcher.run(spec);
    removeQuietly(output);  // the preheat's document is never used
    if (const Mapped mapped = mapChild(result, "preheat"); mapped.exitCode != 0) {
      out.exitCode = mapped.exitCode;
      out.message = mapped.message;
      return out;
    }
  }

  for (unsigned int i = 1; i <= plan.runs; ++i) {
    const std::string what = "run " + std::to_string(i);
    const std::string output = (fs::path(plan.tempDir) / ("run_" + std::to_string(i) + ".json")).string();
    ChildSpec spec;
    spec.args = childArguments(plan, output, 0.0);
    spec.timeoutSeconds = plan.childTimeoutSeconds;
    const ChildResult result = launcher.run(spec);
    if (const Mapped mapped = mapChild(result, what); mapped.exitCode != 0) {
      removeQuietly(output);
      out.exitCode = mapped.exitCode;
      out.message = mapped.message;
      return out;
    }
    std::optional<Value> document = readDocument(output);
    removeQuietly(output);
    if (!document) {
      out.exitCode = 2;
      out.message = what + ": the benchmark process wrote no valid document";
      return out;
    }
    out.runDocuments.push_back(std::move(*document));
  }
  if (plan.canary) {
    // Let the GPU settle out of the benchmark children's tail before reading the machine's state.
    if (plan.canarySettleSeconds > 0.0) clock.sleepSeconds(plan.canarySettleSeconds);
    if (!measureCanary(plan, launcher, "end", out)) return out;
  }
  return out;
}

}  // namespace atlantis::runtime::gate

#include "gate_run.h"

#include "benchmark_run.h"
#include "gate_evaluate.h"
#include "gate_policy.h"
#include "gate_set.h"
#include "gate_store.h"

#include <atlantis/connection/json.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifndef ATLANTIS_PERFORMANCE_DIR
#define ATLANTIS_PERFORMANCE_DIR "tests/performance"
#endif

namespace atlantis::runtime::gate {

namespace {

namespace bench = atlantis::runtime::benchmark;
namespace json = atlantis::connection::json;
namespace fs = std::filesystem;
using atlantis::runtime::cli::GateRequest;

// Removes the directory the children wrote to, whatever way the run ends.
class TempDirScope {
 public:
  explicit TempDirScope(std::string path) : path_(std::move(path)) {
    std::error_code ignored;
    fs::create_directories(path_, ignored);
  }
  ~TempDirScope() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
  TempDirScope(const TempDirScope&) = delete;
  TempDirScope& operator=(const TempDirScope&) = delete;

 private:
  std::string path_;
};

[[nodiscard]] bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << content;
  return static_cast<bool>(out);
}

// A few lines for a person: the verdict, what moved, and why it could not be judged.
[[nodiscard]] std::string summarizeVerdict(const Value& verdict) {
  std::ostringstream out;
  const Value* result = verdict.find("verdict");
  const auto exitCode = numberOf(verdict.find("exit_code"));
  out << "verdict: " << (result != nullptr && result->isString() ? result->asString() : "?") << " (exit "
      << (exitCode ? static_cast<int>(*exitCode) : -1) << ")\n";
  if (const Value* profile = at(verdict, {"baseline", "profile"}); profile != nullptr && profile->isString()) {
    if (!profile->asString().empty()) out << "baseline: " << profile->asString() << "\n";
  }
  if (const Value* reasons = verdict.find("reasons"); reasons != nullptr && reasons->isArray()) {
    for (const Value& reason : reasons->asArray()) out << "  not judged: " << reason.asString() << "\n";
  }
  const Value* metrics = verdict.find("metrics");
  const auto list = [&](std::string_view heading, std::string_view member) {
    const Value* names = at(verdict, {"summary", member});
    if (names == nullptr || !names->isArray()) return;
    for (const Value& name : names->asArray()) {
      out << "  " << heading << ": " << name.asString();
      if (const Value* m = metrics != nullptr ? metrics->find(name.asString()) : nullptr) {
        const auto base = numberOf(m->find("baseline"));
        const auto cand = numberOf(m->find("candidate"));
        const auto pct = numberOf(m->find("delta_pct"));
        if (base && cand) out << " " << formatNumber(*base) << " -> " << formatNumber(*cand);
        if (pct) out << " (" << (*pct >= 0 ? "+" : "") << formatNumber(*pct) << " %)";
        const Value* tolerance = m->find("tolerance");
        const Value* source = m->find("tolerance_source");
        if (tolerance != nullptr && source != nullptr && source->isString()) {
          out << ", tolerance " << formatNumber(*numberOf(tolerance)) << " [" << source->asString() << "]";
        }
      }
      out << "\n";
    }
  };
  list("regressed", "regressed");
  list("improved", "improved");
  if (const Value* exceeded = at(verdict, {"dispersion", "exceeded"}); exceeded != nullptr && exceeded->isArray()) {
    for (const Value& name : exceeded->asArray()) out << "  noisy session: " << name.asString() << "\n";
  }
  return out.str();
}

[[nodiscard]] int runProtocol(const GateRequest& request, GateEnvironment& env) {
  std::ostream& out = *env.out;
  std::ostream& err = *env.err;

  const std::string performanceDir = env.performanceDir;
  const std::string policyPath = request.policyPath.value_or(performanceDir + "/gate_policy.json");
  const std::string baselinesDir = request.baselinesDir.value_or(performanceDir + "/baselines");

  const auto loaded = loadPolicy(policyPath);
  if (loaded.isErr()) {
    err << "atlantis_runtime: --gate: " << loaded.error() << "\n";
    return bench::kExitInitFailure;
  }
  const GatePolicy& policy = loaded.value();

  RunPlan plan;
  plan.scene = request.sceneName;
  plan.variant = request.variant;
  plan.width = request.width;
  plan.height = request.height;
  plan.runs = request.runs.value_or(policy.protocol.runs);
  plan.preheatSeconds = request.preheatSeconds.value_or(policy.protocol.preheatSeconds);
  plan.idleWait = request.idleWait;
  plan.childTimeoutSeconds = policy.protocol.childTimeoutSeconds;
  plan.idle = policy.protocol.idle;
  plan.tempDir = env.tempDir;

  const TempDirScope tempDir(env.tempDir);
  SessionResult session = runSession(plan, *env.launcher, *env.sampler, *env.clock);
  if (session.exitCode != 0) {
    err << "atlantis_runtime: --gate: " << session.message << "\n";
    return session.exitCode;
  }

  Protocol protocol;
  protocol.runs = plan.runs;
  protocol.preheatSeconds = plan.preheatSeconds;
  protocol.idleWait = plan.idleWait;
  protocol.k = policy.protocol.k;
  const auto set = buildSet(protocol, session.runDocuments);
  if (set.isErr()) {
    err << "atlantis_runtime: --gate: the runs do not make a set: " << set.error() << "\n";
    return bench::kExitRuntimeFailure;
  }
  const Value& candidate = set.value();

  // Which baseline: this machine's profile (or the one --profile names), then the file for the
  // scene, variant and resolution the runs actually used.
  const auto profiles = listProfiles(baselinesDir);
  if (profiles.isErr()) {
    err << "atlantis_runtime: --gate: the baseline store is invalid: " << profiles.error() << "\n";
    return bench::kExitInitFailure;
  }
  const Value identity = identityOf(candidate);
  const Profile* profile = nullptr;
  if (request.profileId.has_value() && *request.profileId != "auto") {
    profile = findProfile(profiles.value(), *request.profileId);
    if (profile == nullptr) {
      err << "atlantis_runtime: --gate: no profile '" << *request.profileId << "' under " << baselinesDir << "\n";
      return bench::kExitInitFailure;
    }
  } else {
    profile = matchProfile(profiles.value(), identity);
  }

  const auto width = static_cast<unsigned int>(numberOf(at(candidate, {"settings", "resolution", "width"})).value_or(request.width));
  const auto height = static_cast<unsigned int>(numberOf(at(candidate, {"settings", "resolution", "height"})).value_or(request.height));

  Verdict verdict;
  if (profile == nullptr) {
    verdict = notJudged(candidate, policy, {"", "", ""},
                        {"no baseline profile under " + baselinesDir + " matches this machine's hardware (" +
                         json::write(identity) +
                         "); record one with --gate --record --reason-category 4 and commit it in a dedicated PR"});
  } else {
    const std::string path = baselinePath(*profile, request.sceneName, std::string(cli::toString(request.variant)), width, height);
    LoadedBaseline baseline = loadBaseline(path);
    if (!baseline.error.empty()) {
      err << "atlantis_runtime: --gate: " << baseline.error << "\n";
      return bench::kExitInitFailure;
    }
    BaselineRef ref;
    ref.profile = profile->id;
    ref.file = path;
    if (!baseline.notFound) {
      if (const Value* commit = at(baseline.document, {"baseline", "recorded_commit"}); commit != nullptr && commit->isString()) {
        ref.commit = commit->asString();
      }
    }
    if (baseline.notFound) {
      verdict = notJudged(candidate, policy, ref,
                          {"profile '" + profile->id + "' has no baseline for " + request.sceneName + "/" +
                           std::string(cli::toString(request.variant)) + " at " + std::to_string(width) + "x" +
                           std::to_string(height) + "; record one with --gate --record"});
    } else {
      verdict = evaluate(candidate, baseline.document, policy, ref);
    }
  }

  const std::string serialized = json::write(verdict.document);
  if (request.outputPath.has_value() && !writeFile(*request.outputPath, serialized + "\n")) {
    err << "atlantis_runtime: --gate: cannot write " << *request.outputPath << "\n";
    return bench::kExitInitFailure;
  }
  const Value* conformant = at(verdict.document, {"protocol", "conformant"});
  if (conformant != nullptr && conformant->isBool() && !conformant->asBool()) {
    err << "atlantis_runtime: --gate: warning: this run did not follow the policy's protocol (a smoke run); "
           "its verdict is not evidence for a PR\n";
  }
  if (request.json) {
    out << serialized << "\n";
  } else {
    out << summarizeVerdict(verdict.document);
  }
  return verdict.exitCode;
}

}  // namespace

int runGateWith(const GateRequest& request, GateEnvironment& environment) {
  std::ostream& err = *environment.err;
  switch (request.mode) {
    case GateRequest::Mode::Gate:
      return runProtocol(request, environment);
    case GateRequest::Mode::Record:
      err << "atlantis_runtime: --gate --record: not yet wired (Plan 0060 M6)\n";
      return bench::kExitRuntimeFailure;
    case GateRequest::Mode::Applies:
      err << "atlantis_runtime: --gate --applies: not yet wired (Plan 0060 M8)\n";
      return bench::kExitRuntimeFailure;
  }
  return bench::kExitRuntimeFailure;
}

int runGate(const GateRequest& request) {
  // --applies judges nothing and needs no device and no profiling build.
  if (request.mode != GateRequest::Mode::Applies) {
#if !defined(ATLANTIS_PROFILING)
    std::cerr << "atlantis_runtime: --gate needs a build with ATLANTIS_PROFILING=ON (this build has none); "
                 "configure with -DATLANTIS_PROFILING=ON\n";
    return bench::kExitEnvironment;
#endif
  }
#if defined(_WIN32)
  if (request.mode != GateRequest::Mode::Applies && !request.scene.has_value()) {
    std::cerr << "atlantis_runtime: --gate: scene '" << request.sceneName
              << "' is not available in this build (the bistro scene needs its content: run "
                 "tools/content/fetch_bistro.ps1 and re-configure)\n";
    return bench::kExitEnvironment;
  }
  const auto launcher = makeProcessLauncher(currentExecutablePath());
  const auto sampler = makeSystemCpuSampler();
  const auto clock = makeSystemClock();
  GateEnvironment environment;
  environment.launcher = launcher.get();
  environment.sampler = sampler.get();
  environment.clock = clock.get();
  environment.tempDir = (fs::temp_directory_path() / ("atlantis_gate_" + std::to_string(static_cast<unsigned long long>(
                              std::chrono::steady_clock::now().time_since_epoch().count())))).string();
  environment.performanceDir = ATLANTIS_PERFORMANCE_DIR;
  environment.out = &std::cout;
  environment.err = &std::cerr;
  return runGateWith(request, environment);
#else
  std::cerr << "atlantis_runtime: --gate is only available on Windows\n";
  return bench::kExitEnvironment;
#endif
}

}  // namespace atlantis::runtime::gate

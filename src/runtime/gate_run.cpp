#include "gate_run.h"

#include "benchmark_run.h"
#include "gate_applies.h"
#include "gate_canary.h"
#include "gate_evaluate.h"
#include "gate_policy.h"
#include "gate_set.h"
#include "gate_store.h"

#include <atlantis/connection/json.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
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

// The power source across one gate invocation, read before and after every session (owner ruling after the
// Plan 0060 M7 study). With the precondition on -- the policy's `require_mains_power`, unless the request says
// `--require-mains off` for a smoke run -- anything but mains is refused with exit 3 and no verdict; either way
// the source is what the documents record.
class PowerWatch {
 public:
  PowerWatch(PowerProbe* probe, bool required) : probe_(probe), required_(required) {}

  // 0 when the run may go on, else the exit code (3) after saying why on `err`.
  [[nodiscard]] int check(std::ostream& err, std::string_view command, std::string_view when) {
    const PowerSource source = probe_ != nullptr ? probe_->read() : PowerSource::Unknown;
    const std::string name(toString(source));
    if (std::find(seen_.begin(), seen_.end(), name) == seen_.end()) seen_.push_back(name);
    if (!required_ || source == PowerSource::Mains) return 0;
    err << "atlantis_runtime: " << command << ": ";
    if (source == PowerSource::Battery) {
      err << (when == "after" ? "the machine went onto battery power during the session"
                              : "the machine is on battery power")
          << " (the GPU runs far slower on battery, a shift no tolerance absorbs): connect the charger and run "
             "again; --require-mains off is for smoke runs\n";
    } else {
      err << "cannot tell whether the machine is on mains power: run on a machine that reports it; "
             "--require-mains off is for smoke runs\n";
    }
    return bench::kExitEnvironment;
  }

  // `mains`, `battery`, `unknown`, or `mixed` when it changed between reads.
  [[nodiscard]] std::string value() const {
    if (seen_.empty()) return "unknown";
    return seen_.size() == 1 ? seen_.front() : "mixed";
  }

 private:
  PowerProbe* probe_;
  bool required_;
  std::vector<std::string> seen_;
};

// Spec 0060 Correction 2026-10-14: the record-time quality bar. A recording whose own spread leaves a minimum
// detectable regression above the policy's bound for a tier cannot gate that tier at the resolution the contract
// promises, and is refused rather than committed; the bound is the policy's, fixed before any recording, so a
// refusal is measurement hygiene and never a way to pick a recording that happens to pass.
[[nodiscard]] std::optional<std::string> recordQualityProblem(const Value& mdr, const RecordPolicy& bar) {
  std::string problem;
  for (const auto& [tier, bound] : {std::pair<std::string_view, double>{"cpu", bar.maxMdrCpuPct},
                                    std::pair<std::string_view, double>{"gpu", bar.maxMdrGpuPct}}) {
    const auto value = numberOf(mdr.find(tier));
    if (!value || *value <= bound) continue;  // an unavailable tier is not gated, so it has no resolution to bound
    if (!problem.empty()) problem += ", ";
    problem += std::string(tier) + " " + formatNumber(std::round(*value * 10.0) / 10.0) + " % (bound " +
               formatNumber(bound) + " %)";
  }
  if (problem.empty()) return std::nullopt;
  return problem;
}

// The canary readings of one session's canary documents, each labelled `<label>.<phase>` (or the phase alone when
// there is no label). An unusable document is a message for the caller's exit 2.
[[nodiscard]] std::optional<std::string> collectCanary(const std::vector<std::pair<std::string, Value>>& documents,
                                                       const std::string& label,
                                                       std::vector<std::pair<std::string, CanaryReading>>& readings) {
  for (const auto& [phase, document] : documents) {
    const auto reading = extractCanaryReading(document);
    if (reading.isErr()) return "the GPU canary (" + phase + ") is not usable: " + reading.error();
    readings.emplace_back(label.empty() ? phase : label + "." + phase, reading.value());
  }
  return std::nullopt;
}

// The start-of-session precondition (Spec 0060 Correction 2026-10-15): when the baseline this session will be judged
// against can be found from the canary child's own machine identity, and the canary is already above that baseline's
// reference by more than the policy's limit, the session is refused before the preheat and the runs are spent
// (exit 3). A machine the store does not recognise is not refused here; the verdict says why it cannot be judged.
[[nodiscard]] std::function<std::optional<std::string>(const Value&)> makeStartCheck(
    const GateRequest& request, const GatePolicy& policy, const std::string& baselinesDir) {
  return [sceneName = request.sceneName, variant = std::string(cli::toString(request.variant)), width = request.width,
          height = request.height, profileId = request.profileId, baselinesDir,
          maxShiftPct = policy.canary.maxShiftPct](const Value& document) -> std::optional<std::string> {
    const auto reading = extractCanaryReading(document);
    const Value* system = canarySystem(document);
    if (reading.isErr() || system == nullptr) return std::nullopt;
    const auto profiles = listProfiles(baselinesDir);
    if (profiles.isErr()) return std::nullopt;
    const Profile* chosen = nullptr;
    int matches = 0;
    for (const Profile& profile : profiles.value()) {
      bool same = true;
      if (profileId.has_value() && *profileId != "auto") {
        same = profile.id == *profileId;
      } else {
        for (const std::string_view key : {"os", "cpu", "gpu", "driver", "vulkan_api"}) {
          const Value* a = system->find(key);
          const Value* b = profile.identity.find(key);
          if (a == nullptr || b == nullptr || !(*a == *b)) same = false;
        }
      }
      if (same) {
        chosen = &profile;
        ++matches;
      }
    }
    if (matches != 1) return std::nullopt;
    const auto baseline = loadBaseline(baselinePath(*chosen, sceneName, variant, width, height));
    if (baseline.notFound || !baseline.error.empty()) return std::nullopt;
    Value probe = Value::object();
    probe.set("canary", canarySetValue({{"start", reading.value()}}));
    const CanaryJudgment judgment = judgeCanary(probe, baseline.document, maxShiftPct);
    if (!judgment.measured || !judgment.shifted) return std::nullopt;
    std::string components;
    for (const std::string& name : judgment.shiftedComponents) {
      if (!components.empty()) components += ", ";
      components += name + " +" + formatNumber(std::round(judgment.shiftPct[canaryComponentIndex(name)] * 10.0) / 10.0) + " %";
    }
    return "at the start of the session the GPU canary ran slower than the baseline's reference (" + components +
           "; limit " + formatNumber(maxShiftPct) +
           " %): the machine's GPU is not in the state the baseline was recorded in. Let the machine settle (the state "
           "usually passes within minutes) and run the gate again";
  };
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
  plan.pin = request.pinProcess.value_or(policy.protocol.pinProcess);
  plan.canary = policy.canary.required;
  plan.canarySettleSeconds = policy.canary.settleSeconds;
  if (plan.canary) plan.startCheck = makeStartCheck(request, policy, baselinesDir);
  plan.tempDir = env.tempDir;

  const TempDirScope tempDir(env.tempDir);
  PowerWatch power(env.power, request.requireMains.value_or(policy.protocol.requireMainsPower));
  if (const int code = power.check(err, "--gate", "before"); code != 0) return code;
  SessionResult session = runSession(plan, *env.launcher, *env.sampler, *env.clock);
  if (session.exitCode != 0) {
    err << "atlantis_runtime: --gate: " << session.message << "\n";
    return session.exitCode;
  }
  if (const int code = power.check(err, "--gate", "after"); code != 0) return code;

  Protocol protocol;
  protocol.runs = plan.runs;
  protocol.preheatSeconds = plan.preheatSeconds;
  protocol.idleWait = plan.idleWait;
  protocol.k = policy.protocol.k;
  protocol.pin = plan.pin;
  protocol.power = power.value();
  auto set = buildSet(protocol, session.runDocuments);
  if (set.isErr()) {
    err << "atlantis_runtime: --gate: the runs do not make a set: " << set.error() << "\n";
    return bench::kExitRuntimeFailure;
  }
  Value& candidate = set.value();
  if (plan.canary) {
    std::vector<std::pair<std::string, CanaryReading>> readings;
    if (const auto problem = collectCanary(session.canaryDocuments, "", readings)) {
      err << "atlantis_runtime: --gate: " << *problem << "\n";
      return bench::kExitRuntimeFailure;
    }
    candidate.set("canary", canarySetValue(readings));
  }

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

// `--gate --record`: the protocol over several sessions, written as a candidate baseline at the path
// the caller named. It never judges and never touches the committed store by itself.
[[nodiscard]] int runRecord(const GateRequest& request, GateEnvironment& env) {
  std::ostream& out = *env.out;
  std::ostream& err = *env.err;

  const std::string policyPath = request.policyPath.value_or(env.performanceDir + "/gate_policy.json");
  const auto loaded = loadPolicy(policyPath);
  if (loaded.isErr()) {
    err << "atlantis_runtime: --gate --record: " << loaded.error() << "\n";
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
  plan.pin = request.pinProcess.value_or(policy.protocol.pinProcess);
  plan.canary = policy.canary.required;
  plan.canarySettleSeconds = policy.canary.settleSeconds;
  plan.tempDir = env.tempDir;

  // A baseline is only ever recorded under the policy's full protocol: a smoke run is not a measurement.
  const bool requireMains = request.requireMains.value_or(policy.protocol.requireMainsPower);
  if (!plan.idleWait || plan.preheatSeconds < policy.protocol.preheatSeconds || plan.runs < policy.protocol.runs ||
      plan.pin != policy.protocol.pinProcess || requireMains != policy.protocol.requireMainsPower) {
    err << "atlantis_runtime: --gate --record needs the policy's full protocol (idle wait on, a preheat of at least "
        << formatNumber(policy.protocol.preheatSeconds) << " s, at least " << policy.protocol.runs
        << " runs per session, children pinned as the policy says, mains power as the policy says); --idle-wait off, a shorter "
           "--preheat-seconds, a smaller --runs, a different --pin-process and a different --require-mains are for "
           "smoke runs\n";
    return bench::kExitInitFailure;
  }
  if (request.profileId.has_value() && *request.profileId != "auto" && !isValidProfileId(*request.profileId)) {
    err << "atlantis_runtime: --gate --record: '" << *request.profileId << "' is not a valid profile id (a-z, 0-9, '-')\n";
    return bench::kExitInitFailure;
  }

  const TempDirScope tempDir(env.tempDir);
  std::vector<Value> documents;
  std::vector<std::string> starts;
  PowerWatch power(env.power, requireMains);
  std::vector<std::pair<std::string, CanaryReading>> canaryReadings;
  for (unsigned int session = 1; session <= request.sessions; ++session) {
    if (session > 1) {
      err << "atlantis_runtime: --gate --record: waiting " << formatNumber(request.sessionGapMinutes)
          << " minutes before session " << session << " of " << request.sessions << "\n";
      env.clock->sleepSeconds(request.sessionGapMinutes * 60.0);
    }
    err << "atlantis_runtime: --gate --record: session " << session << " of " << request.sessions << "\n";
    if (const int code = power.check(err, "--gate --record", "before"); code != 0) return code;
    starts.push_back(env.clock->isoUtc());
    SessionResult result = runSession(plan, *env.launcher, *env.sampler, *env.clock);
    if (result.exitCode != 0) {
      err << "atlantis_runtime: --gate --record: " << result.message << "\n";
      return result.exitCode;
    }
    if (const int code = power.check(err, "--gate --record", "after"); code != 0) return code;
    if (plan.canary) {
      if (const auto problem = collectCanary(result.canaryDocuments, "s" + std::to_string(session), canaryReadings)) {
        err << "atlantis_runtime: --gate --record: " << *problem << "\n";
        return bench::kExitRuntimeFailure;
      }
    }
    for (Value& document : result.runDocuments) documents.push_back(std::move(document));
  }

  Protocol protocol;
  protocol.runs = plan.runs;
  protocol.preheatSeconds = plan.preheatSeconds;
  protocol.idleWait = plan.idleWait;
  protocol.k = policy.protocol.k;
  protocol.pin = plan.pin;
  protocol.power = power.value();
  auto set = buildSet(protocol, documents);
  if (set.isErr()) {
    err << "atlantis_runtime: --gate --record: the runs do not make a set (did the hardware or build change "
           "between sessions?): "
        << set.error() << "\n";
    return bench::kExitRuntimeFailure;
  }
  if (plan.canary) {
    set.value().set("canary", canarySetValue(canaryReadings));
    // A recording made in one GPU state: its canary readings agree. One that straddled two states is a baseline for
    // neither (the same objective bar as the minimum detectable regression, fixed in the policy beforehand).
    const double spread = canarySpreadPct(set.value());
    if (spread < 0.0 || spread > policy.record.maxCanarySpreadPct) {
      err << "atlantis_runtime: --gate --record: refused, nothing was written: this recording's GPU canary readings "
          << "spread " << formatNumber(std::round(spread * 10.0) / 10.0) << " % across its sessions (bound "
          << formatNumber(policy.record.maxCanarySpreadPct)
          << " %), so the machine's GPU was not in one state while it was recorded. Let the machine settle, stay on "
             "mains power, and record again; the bound is the policy's, fixed before any recording, and is not "
             "raised to make a recording pass\n";
      return bench::kExitEnvironment;
    }
  }

  const Value identity = identityOf(set.value());
  Profile profile;
  profile.id = request.profileId.has_value() && *request.profileId != "auto" ? *request.profileId : deriveProfileId(identity);
  profile.identity = identity;
  std::string label;
  for (const std::string_view key : {"gpu", "cpu", "os", "driver"}) {
    if (const Value* value = identity.find(key); value != nullptr && value->isString()) {
      if (!label.empty()) label += " / ";
      label += value->asString();
    }
  }

  BaselineProvenance provenance;
  provenance.profile = profile.id;
  if (const Value* revision = at(set.value(), {"meta", "build", "git_revision"}); revision != nullptr && revision->isString()) {
    provenance.recordedCommit = revision->asString();
  }
  if (const Value* dirty = at(set.value(), {"meta", "build", "git_dirty"}); dirty != nullptr && dirty->isBool()) {
    provenance.gitDirty = dirty->asBool();
  }
  provenance.recordedAt = env.clock->isoUtc();
  provenance.sessionStarts = starts;
  provenance.runsPerSession = plan.runs;
  provenance.reasonCategory = request.reasonCategory;
  provenance.reasonText = request.reasonText;
  if (provenance.gitDirty) {
    err << "atlantis_runtime: --gate --record: warning: the build was made from a tree with uncommitted changes; "
           "a baseline for the committed store is recorded from a clean build of the commit it names\n";
  }

  Value mdr = computeMdr(set.value(), policy, plan.runs);
  if (const auto problem = recordQualityProblem(mdr, policy.record)) {
    err << "atlantis_runtime: --gate --record: refused, nothing was written: this recording is too noisy to gate at "
           "the resolution the contract promises (minimum detectable regression "
        << *problem
        << "). Its sessions' run-level values spread more than the policy's record bar (record.max_mdr_*_pct) allows, "
           "usually because the machine was not quiet or a CPU or GPU performance state changed between the sessions. "
           "Close other applications, stay on mains power, let the machine settle, and record again; the bar is the "
           "policy's, fixed before any recording, and is not raised to make a recording pass\n";
    return bench::kExitEnvironment;
  }
  Value baseline = makeBaseline(std::move(set.value()), provenance, std::move(mdr));

  const auto width = static_cast<unsigned int>(numberOf(at(baseline, {"settings", "resolution", "width"})).value_or(request.width));
  const auto height = static_cast<unsigned int>(numberOf(at(baseline, {"settings", "resolution", "height"})).value_or(request.height));
  const auto written = writeCandidate(request.recordTo, profile, baseline, request.sceneName,
                                      std::string(cli::toString(request.variant)), width, height, label);
  if (written.isErr()) {
    err << "atlantis_runtime: --gate --record: " << written.error() << "\n";
    return bench::kExitInitFailure;
  }

  const std::string serialized = json::write(baseline);
  if (request.outputPath.has_value() && !writeFile(*request.outputPath, serialized + "\n")) {
    err << "atlantis_runtime: --gate --record: cannot write " << *request.outputPath << "\n";
    return bench::kExitInitFailure;
  }
  if (request.json) {
    out << serialized << "\n";
  } else {
    out << "recorded baseline for profile " << profile.id << (written.value().profileCreated ? " (new profile)" : "")
        << "\n  baseline: " << written.value().baselineFile << "\n  profile:  " << written.value().profileFile
        << "\n  runs: " << documents.size() << " in " << request.sessions << " sessions; commit "
        << (provenance.recordedCommit.empty() ? "unknown" : provenance.recordedCommit)
        << (provenance.gitDirty ? "+dirty" : "") << "\n  reason (category " << request.reasonCategory
        << "): " << request.reasonText << "\n";
    if (const Value* cpu = at(baseline, {"mdr", "cpu"}); cpu != nullptr && cpu->isNumber()) {
      out << "  minimum detectable regression: cpu " << formatNumber(*numberOf(cpu)) << " %";
      if (const Value* gpu = at(baseline, {"mdr", "gpu"}); gpu != nullptr && gpu->isNumber()) {
        out << ", gpu " << formatNumber(*numberOf(gpu)) << " %";
      }
      out << "\n";
    }
  }
  return bench::kExitOk;
}

// `--gate --applies`: one changed path per line on stdin; prints {"applies", "matched"}. It judges
// nothing, so a docs-only change is exit 0 with applies false -- and an unreadable file is never a
// quiet "no".
[[nodiscard]] int runApplies(const GateRequest& request, GateEnvironment& env) {
  const std::string path = request.applicabilityPath.value_or(env.performanceDir + "/applicability.txt");
  const auto loaded = loadApplicability(path);
  if (loaded.isErr()) {
    *env.err << "atlantis_runtime: --gate --applies: " << loaded.error() << "\n";
    return bench::kExitInitFailure;
  }
  std::vector<std::string> paths;
  std::string line;
  while (env.in != nullptr && std::getline(*env.in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) paths.push_back(line);
  }
  *env.out << json::write(toValue(evaluateApplicability(loaded.value(), paths))) << "\n";
  return bench::kExitOk;
}

}  // namespace

int runGateWith(const GateRequest& request, GateEnvironment& environment) {
  switch (request.mode) {
    case GateRequest::Mode::Gate:
      return runProtocol(request, environment);
    case GateRequest::Mode::Record:
      return runRecord(request, environment);
    case GateRequest::Mode::Applies:
      return runApplies(request, environment);
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
  const auto power = makeSystemPowerProbe();
  const auto clock = makeSystemClock();
  GateEnvironment environment;
  environment.launcher = launcher.get();
  environment.sampler = sampler.get();
  environment.power = power.get();
  environment.clock = clock.get();
  environment.tempDir = (fs::temp_directory_path() / ("atlantis_gate_" + std::to_string(static_cast<unsigned long long>(
                              std::chrono::steady_clock::now().time_since_epoch().count())))).string();
  environment.performanceDir = ATLANTIS_PERFORMANCE_DIR;
  environment.in = &std::cin;
  environment.out = &std::cout;
  environment.err = &std::cerr;
  return runGateWith(request, environment);
#else
  std::cerr << "atlantis_runtime: --gate is only available on Windows\n";
  return bench::kExitEnvironment;
#endif
}

}  // namespace atlantis::runtime::gate

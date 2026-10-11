#include "gate_evaluate.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>
#include <cmath>
#include <map>
#include <optional>

namespace atlantis::runtime::gate {

namespace {

struct Field {
  const char* name;
  std::vector<std::string_view> path;
};

// Everything that must be equal for two runs to be judged against each other: the
// fields Spec 0059's comparison checks, plus the CPU, OS, Vulkan version, present
// mode and refresh rate that a hardware profile is made of (Spec 0060 R7, Q5).
const std::vector<Field> kIdentity = {
    {"scene.guid", {"scene", "guid"}},
    {"scene.content_pin", {"scene", "content_pin"}},
    {"settings.resolution.width", {"settings", "resolution", "width"}},
    {"settings.resolution.height", {"settings", "resolution", "height"}},
    {"settings.variant", {"settings", "variant"}},
    {"settings.gpu_timing", {"settings", "gpu_timing"}},
    {"meta.build.type", {"meta", "build", "type"}},
    {"meta.build.profiling", {"meta", "build", "profiling"}},
    {"meta.build.tracy", {"meta", "build", "tracy"}},
    {"meta.build.gpu_markers", {"meta", "build", "gpu_markers"}},
    {"meta.build.validation", {"meta", "build", "validation"}},
    {"meta.system.os", {"meta", "system", "os"}},
    {"meta.system.cpu", {"meta", "system", "cpu"}},
    {"meta.system.gpu", {"meta", "system", "gpu"}},
    {"meta.system.driver", {"meta", "system", "driver"}},
    {"meta.system.vulkan_api", {"meta", "system", "vulkan_api"}},
    {"meta.display.present_mode", {"meta", "display", "present_mode"}},
    {"meta.display.refresh_hz", {"meta", "display", "refresh_hz"}},
};

[[nodiscard]] std::string describe(const Value* value) {
  if (value == nullptr) return "absent";
  if (value->isString()) return value->asString();
  if (value->isNumber()) return value->numberText();
  if (value->isBool()) return value->asBool() ? "true" : "false";
  return "null";
}

// What a metric's aggregate says, or why it says nothing.
struct Aggregate {
  bool available = false;
  double center = 0.0;
  double sigma = 0.0;
  double n = 0.0;
};

[[nodiscard]] Aggregate readAggregate(const Value& set, const std::string& key) {
  Aggregate out;
  const Value* entry = at(set, {"aggregate", key});
  if (entry == nullptr) return out;
  const auto center = numberOf(entry->find("center"));
  if (!center) return out;
  out.available = true;
  out.center = *center;
  out.sigma = numberOf(entry->find("sigma")).value_or(0.0);
  out.n = numberOf(entry->find("n")).value_or(0.0);
  return out;
}

[[nodiscard]] std::vector<std::string> aggregateKeys(const Value& set) {
  std::vector<std::string> keys;
  if (const Value* aggregate = set.find("aggregate"); aggregate != nullptr && aggregate->isObject()) {
    for (const auto& [key, value] : aggregate->asObject()) keys.push_back(key);
  }
  return keys;
}

[[nodiscard]] Value stringArray(const std::vector<std::string>& items) {
  Value out = Value::array();
  for (const std::string& item : items) out.push(Value::string(item));
  return out;
}

[[nodiscard]] Value protocolValue(const Value& candidate, const GatePolicy& policy) {
  const double runs = numberOf(at(candidate, {"protocol", "runs"})).value_or(0.0);
  const double preheat = numberOf(at(candidate, {"protocol", "preheat_seconds"})).value_or(0.0);
  const double k = numberOf(at(candidate, {"protocol", "k"})).value_or(0.0);
  const Value* idle = at(candidate, {"protocol", "idle_wait"});
  const bool idleWait = idle != nullptr && idle->isBool() && idle->asBool();
  // A run that skipped the idle wait or the preheat, or took fewer runs than the
  // policy asks, is a smoke run: it is reported as such and never a recording.
  const bool conformant = idleWait && preheat >= policy.protocol.preheatSeconds && runs >= policy.protocol.runs &&
                          k == policy.protocol.k;
  Value out = Value::object();
  out.set("runs", num(runs));
  out.set("preheat_seconds", num(preheat));
  out.set("idle_wait", Value::boolean(idleWait));
  out.set("k", num(k));
  out.set("conformant", Value::boolean(conformant));
  return out;
}

[[nodiscard]] Value baselineValue(const BaselineRef& ref) {
  Value out = Value::object();
  out.set("profile", Value::string(ref.profile));
  out.set("file", Value::string(ref.file));
  out.set("commit", Value::string(ref.commit));
  return out;
}

[[nodiscard]] Value changeValue(double baseline, double candidate) {
  Value out = Value::object();
  out.set("baseline", num(baseline));
  out.set("candidate", num(candidate));
  out.set("delta", num(candidate - baseline));
  out.set("delta_pct", baseline != 0.0 ? num((candidate - baseline) / baseline * 100.0) : Value());
  return out;
}

struct Entry {
  std::string key;
  Tier tier = Tier::Counter;
  std::string status;  // ok, regressed, improved, ignored_floor, unavailable
  Value value;
};

struct Judged {
  std::string status = "ok";
  double tolerance = 0.0;
  std::string source = "exact";
};

[[nodiscard]] Judged judge(const GatePolicy& policy, Tier tier, const std::string& key, const Aggregate& base,
                           const Aggregate& cand) {
  const TierPolicy& tp = policy.tierPolicy(tier);
  const double delta = cand.center - base.center;
  Judged out;
  if (tier == Tier::Counter) {
    // Deterministic: exact, unless the policy allows this counter a small relative spread.
    const auto it = tp.metricTolerancePct.find(key);
    out.tolerance = it != tp.metricTolerancePct.end() ? std::abs(base.center) * it->second / 100.0 : 0.0;
    out.source = it != tp.metricTolerancePct.end() ? "relative" : "exact";
    if (delta > out.tolerance) out.status = "regressed";
    else if (-delta > out.tolerance) out.status = "improved";
    return out;
  }
  const double pctTerm = std::abs(base.center) * tp.maxPct / 100.0;
  const double count = std::max(1.0, base.n);
  const double runs = std::max(1.0, cand.n);
  const double noiseTerm = policy.protocol.k * base.sigma * 1.25 * std::sqrt(1.0 / count + 1.0 / runs);
  out.tolerance = tp.absFloor;
  out.source = "floor";
  if (pctTerm > out.tolerance) {
    out.tolerance = pctTerm;
    out.source = "max_pct";
  }
  if (noiseTerm > out.tolerance) {
    out.tolerance = noiseTerm;
    out.source = "noise";
  }
  if (delta > out.tolerance) {
    out.status = "regressed";
  } else if (-delta > out.tolerance) {
    out.status = "improved";
  } else {
    // The floor alone absorbed a change the relative and noise terms would have flagged.
    const double relative = std::max(pctTerm, noiseTerm);
    if (std::abs(delta) > relative && std::abs(delta) <= tp.absFloor) out.status = "ignored_floor";
  }
  return out;
}

[[nodiscard]] Verdict finish(std::string result, Value document) {
  Verdict verdict;
  verdict.result = std::move(result);
  verdict.exitCode = exitCodeFor(verdict.result);
  document.set("exit_code", Value::number(static_cast<std::int64_t>(verdict.exitCode)));
  verdict.document = std::move(document);
  return verdict;
}

[[nodiscard]] Value emptySummary() {
  Value summary = Value::object();
  summary.set("regressed", Value::array());
  summary.set("improved", Value::array());
  summary.set("ignored", Value::array());
  summary.set("unavailable", Value::array());
  summary.set("ok", count(0));
  return summary;
}

}  // namespace

int exitCodeFor(std::string_view result) noexcept {
  if (result == "pass") return kExitPass;
  if (result == "fail") return kExitFail;
  return kExitNotJudged;
}

Verdict notJudged(const Value& candidate, const GatePolicy& policy, const BaselineRef& baselineRef,
                  std::vector<std::string> reasons) {
  Value doc = Value::object();
  doc.set("schema", Value::string(std::string(kGateSchema)));
  doc.set("verdict", Value::string("incomparable"));
  doc.set("exit_code", Value());  // replaced by finish()
  doc.set("baseline", baselineValue(baselineRef));
  doc.set("protocol", protocolValue(candidate, policy));
  doc.set("comparable", Value::boolean(false));
  doc.set("reasons", stringArray(reasons));
  doc.set("metrics", Value::object());
  Value dispersion = Value::object();
  dispersion.set("factor", num(policy.dispersionFactor));
  dispersion.set("exceeded", Value::array());
  doc.set("dispersion", std::move(dispersion));
  doc.set("reported", Value::object());
  doc.set("summary", emptySummary());
  return finish("incomparable", std::move(doc));
}

Verdict evaluate(const Value& candidate, const Value& baseline, const GatePolicy& policy,
                 const BaselineRef& baselineRef) {
  // (1) Identity and workload: unlike runs are not judged.
  std::vector<std::string> reasons;
  for (const Field& field : kIdentity) {
    const Value* b = at(baseline, std::span<const std::string_view>(field.path));
    const Value* c = at(candidate, std::span<const std::string_view>(field.path));
    const bool bothAbsent = b == nullptr && c == nullptr;
    if (!bothAbsent && (b == nullptr || c == nullptr || !(*b == *c))) {
      reasons.push_back(std::string(field.name) + " differs: baseline " + describe(b) + ", current " + describe(c));
    }
  }
  for (const std::string& key : aggregateKeys(baseline)) {
    if (!isWorkload(policy, key)) continue;
    const Aggregate b = readAggregate(baseline, key);
    const Aggregate c = readAggregate(candidate, key);
    if (b.available && c.available && b.center != c.center) {
      reasons.push_back("workload changed: " + key + " baseline " + formatNumber(b.center) + ", current " +
                        formatNumber(c.center) + " (the scene is not the one the baseline measured)");
    }
  }
  if (!reasons.empty()) return notJudged(candidate, policy, baselineRef, std::move(reasons));

  // (2)-(4) Per metric, in the order the baseline lists them (sorted keys).
  Value metrics = Value::object();
  std::vector<std::string> regressed, improved, ignored, unavailable;
  std::vector<std::string> counterRegressed, exceeded;
  std::size_t ok = 0;
  Value reported = Value::object();

  for (const std::string& key : aggregateKeys(baseline)) {
    const Aggregate b = readAggregate(baseline, key);
    const Aggregate c = readAggregate(candidate, key);
    if (const auto tier = gatedTier(policy, key)) {
      Value entry = Value::object();
      entry.set("tier", Value::string(std::string(toString(*tier))));
      if (!b.available || !c.available) {
        entry.set("status", Value::string("unavailable"));
        metrics.set(key, std::move(entry));
        unavailable.push_back(key);
        continue;
      }
      const Judged judged = judge(policy, *tier, key, b, c);
      const Value change = changeValue(b.center, c.center);
      for (const auto& [name, value] : change.asObject()) entry.set(name, value);
      entry.set("tolerance", num(judged.tolerance));
      entry.set("tolerance_source", Value::string(judged.source));
      entry.set("status", Value::string(judged.status));
      metrics.set(key, std::move(entry));
      if (judged.status == "regressed") {
        regressed.push_back(key);
        if (*tier == Tier::Counter) counterRegressed.push_back(key);
      } else if (judged.status == "improved") {
        improved.push_back(key);
      } else if (judged.status == "ignored_floor") {
        ignored.push_back(key);
        ++ok;
      } else {
        ++ok;
      }
      // Dispersion: only the noisy tiers; a deterministic counter has none to speak of.
      if (*tier != Tier::Counter) {
        const TierPolicy& tp = policy.tierPolicy(*tier);
        const double reference = std::max(b.sigma, tp.absFloor / policy.protocol.k);
        if (c.sigma > policy.dispersionFactor * reference) exceeded.push_back(key);
      }
    } else if (isReported(policy, key) && b.available && c.available) {
      reported.set(key, changeValue(b.center, c.center));
    }
  }

  Value summary = Value::object();
  summary.set("regressed", stringArray(regressed));
  summary.set("improved", stringArray(improved));
  summary.set("ignored", stringArray(ignored));
  summary.set("unavailable", stringArray(unavailable));
  summary.set("ok", count(ok));

  std::string result = "pass";
  if (!counterRegressed.empty()) {
    result = "fail";
  } else if (!exceeded.empty()) {
    result = "inconclusive";
  } else if (!regressed.empty()) {
    result = "fail";
  }

  Value doc = Value::object();
  doc.set("schema", Value::string(std::string(kGateSchema)));
  doc.set("verdict", Value::string(result));
  doc.set("exit_code", Value());  // replaced by finish()
  doc.set("baseline", baselineValue(baselineRef));
  doc.set("protocol", protocolValue(candidate, policy));
  doc.set("comparable", Value::boolean(true));
  doc.set("reasons", Value::array());
  doc.set("metrics", std::move(metrics));
  Value dispersion = Value::object();
  dispersion.set("factor", num(policy.dispersionFactor));
  dispersion.set("exceeded", stringArray(exceeded));
  doc.set("dispersion", std::move(dispersion));
  doc.set("reported", std::move(reported));
  doc.set("summary", std::move(summary));
  return finish(result, std::move(doc));
}

}  // namespace atlantis::runtime::gate

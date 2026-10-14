#include "gate_policy.h"

#include <fstream>
#include <iterator>

namespace atlantis::runtime::gate {

namespace json = atlantis::connection::json;
using PolicyResult = atlantis::Result<GatePolicy, std::string>;

std::string_view toString(Tier tier) noexcept {
  switch (tier) {
    case Tier::Counter: return "counter";
    case Tier::Cpu: return "cpu";
    case Tier::Gpu: return "gpu";
  }
  return "counter";
}

const TierPolicy& GatePolicy::tierPolicy(Tier tier) const noexcept {
  switch (tier) {
    case Tier::Counter: return counter;
    case Tier::Cpu: return cpu;
    case Tier::Gpu: return gpu;
  }
  return counter;
}

namespace {

[[nodiscard]] std::optional<std::string> readNumber(const Value& object, std::string_view member, double minimum,
                                                    double& out) {
  const Value* value = object.find(member);
  const auto number = numberOf(value);
  if (!number) return "'" + std::string(member) + "' is missing or not a number";
  if (*number < minimum) return "'" + std::string(member) + "' is below " + formatNumber(minimum);
  out = *number;
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> readStrings(const Value& object, std::string_view member,
                                                     std::vector<std::string>& out) {
  const Value* value = object.find(member);
  if (value == nullptr || !value->isArray()) return "'" + std::string(member) + "' is missing or not an array";
  out.clear();
  for (const Value& item : value->asArray()) {
    if (!item.isString() || item.asString().empty()) return "'" + std::string(member) + "' holds a non-string entry";
    out.push_back(item.asString());
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> readTier(const Value& tiers, std::string_view name, TierPolicy& out) {
  const Value* tier = tiers.find(name);
  if (tier == nullptr || !tier->isObject()) return "tiers." + std::string(name) + " is missing";
  if (auto error = readNumber(*tier, "abs_floor", 0.0, out.absFloor)) return "tiers." + std::string(name) + ": " + *error;
  if (auto error = readNumber(*tier, "max_pct", 0.0, out.maxPct)) return "tiers." + std::string(name) + ": " + *error;
  if (const Value* per = tier->find("metric_tolerance_pct")) {
    if (!per->isObject()) return "tiers." + std::string(name) + ".metric_tolerance_pct is not an object";
    for (const auto& [metric, value] : per->asObject()) {
      const auto number = numberOf(&value);
      if (!number || *number < 0.0) return "tiers." + std::string(name) + ".metric_tolerance_pct." + metric + " is invalid";
      out.metricTolerancePct[metric] = *number;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool anyMatch(const std::vector<std::string>& patterns, std::string_view key) {
  for (const std::string& pattern : patterns) {
    if (matchesPattern(pattern, key)) return true;
  }
  return false;
}

}  // namespace

PolicyResult parsePolicy(const Value& document) {
  if (!document.isObject()) return PolicyResult::Err("the policy is not a JSON object");
  GatePolicy policy;

  double number = 0.0;
  if (auto error = readNumber(document, "version", 1.0, number)) return PolicyResult::Err(*error);
  policy.version = static_cast<unsigned int>(number);

  const Value* protocol = document.find("protocol");
  if (protocol == nullptr || !protocol->isObject()) return PolicyResult::Err("'protocol' is missing");
  if (auto error = readNumber(*protocol, "runs", 1.0, number)) return PolicyResult::Err("protocol: " + *error);
  policy.protocol.runs = static_cast<unsigned int>(number);
  if (auto error = readNumber(*protocol, "preheat_seconds", 0.0, policy.protocol.preheatSeconds)) {
    return PolicyResult::Err("protocol: " + *error);
  }
  if (auto error = readNumber(*protocol, "k", 0.0, policy.protocol.k)) return PolicyResult::Err("protocol: " + *error);
  if (policy.protocol.k <= 0.0) return PolicyResult::Err("protocol: 'k' must be positive");
  if (auto error = readNumber(*protocol, "child_timeout_seconds", 1.0, policy.protocol.childTimeoutSeconds)) {
    return PolicyResult::Err("protocol: " + *error);
  }
  const Value* pin = protocol->find("pin_process");
  if (pin == nullptr || !pin->isBool()) return PolicyResult::Err("protocol: 'pin_process' is missing or not a boolean");
  policy.protocol.pinProcess = pin->asBool();
  const Value* mains = protocol->find("require_mains_power");
  if (mains == nullptr || !mains->isBool()) {
    return PolicyResult::Err("protocol: 'require_mains_power' is missing or not a boolean");
  }
  policy.protocol.requireMainsPower = mains->asBool();
  const Value* idle = protocol->find("idle");
  if (idle == nullptr || !idle->isObject()) return PolicyResult::Err("protocol.idle is missing");
  if (auto error = readNumber(*idle, "cpu_idle_pct", 0.0, policy.protocol.idle.cpuIdlePct)) {
    return PolicyResult::Err("protocol.idle: " + *error);
  }
  if (policy.protocol.idle.cpuIdlePct > 100.0) return PolicyResult::Err("protocol.idle: 'cpu_idle_pct' is above 100");
  if (auto error = readNumber(*idle, "window_seconds", 0.0, policy.protocol.idle.windowSeconds)) {
    return PolicyResult::Err("protocol.idle: " + *error);
  }
  if (auto error = readNumber(*idle, "timeout_seconds", 0.0, policy.protocol.idle.timeoutSeconds)) {
    return PolicyResult::Err("protocol.idle: " + *error);
  }

  if (auto error = readNumber(document, "dispersion_factor", 1.0, policy.dispersionFactor)) {
    return PolicyResult::Err(*error);
  }

  const Value* tiers = document.find("tiers");
  if (tiers == nullptr || !tiers->isObject()) return PolicyResult::Err("'tiers' is missing");
  if (auto error = readTier(*tiers, "counter", policy.counter)) return PolicyResult::Err(*error);
  if (auto error = readTier(*tiers, "cpu", policy.cpu)) return PolicyResult::Err(*error);
  if (auto error = readTier(*tiers, "gpu", policy.gpu)) return PolicyResult::Err(*error);

  const Value* gated = document.find("gated");
  if (gated == nullptr || !gated->isObject()) return PolicyResult::Err("'gated' is missing");
  if (auto error = readStrings(*gated, "counter", policy.gatedCounter)) return PolicyResult::Err("gated: " + *error);
  if (auto error = readStrings(*gated, "cpu", policy.gatedCpu)) return PolicyResult::Err("gated: " + *error);
  if (auto error = readStrings(*gated, "gpu", policy.gatedGpu)) return PolicyResult::Err("gated: " + *error);
  if (auto error = readStrings(document, "reported", policy.reported)) return PolicyResult::Err(*error);
  if (auto error = readStrings(document, "workload", policy.workload)) return PolicyResult::Err(*error);

  if (auto error = readStrings(document, "dispersion_metrics", policy.dispersionMetrics)) {
    return PolicyResult::Err(*error);
  }
  if (policy.dispersionMetrics.empty()) return PolicyResult::Err("'dispersion_metrics' is empty");
  for (const std::string& metric : policy.dispersionMetrics) {
    // A literal entry must name a gated CPU or GPU metric; a dispersion check on anything else would never run.
    if (metric.find('*') != std::string::npos) continue;
    const auto tier = gatedTier(policy, metric);
    if (!tier || *tier == Tier::Counter) {
      return PolicyResult::Err("'dispersion_metrics' entry '" + metric + "' is not a gated CPU or GPU metric");
    }
  }

  const Value* record = document.find("record");
  if (record == nullptr || !record->isObject()) return PolicyResult::Err("'record' is missing");
  if (auto error = readNumber(*record, "max_mdr_cpu_pct", 0.0, policy.record.maxMdrCpuPct)) {
    return PolicyResult::Err("record: " + *error);
  }
  if (auto error = readNumber(*record, "max_mdr_gpu_pct", 0.0, policy.record.maxMdrGpuPct)) {
    return PolicyResult::Err("record: " + *error);
  }
  if (policy.record.maxMdrCpuPct <= 0.0 || policy.record.maxMdrGpuPct <= 0.0) {
    return PolicyResult::Err("record: the bounds must be positive");
  }
  if (auto error = readNumber(*record, "max_canary_spread_pct", 0.0, policy.record.maxCanarySpreadPct)) {
    return PolicyResult::Err("record: " + *error);
  }
  if (policy.record.maxCanarySpreadPct <= 0.0) return PolicyResult::Err("record: the canary spread bound must be positive");

  const Value* canary = document.find("canary");
  if (canary == nullptr || !canary->isObject()) return PolicyResult::Err("'canary' is missing");
  if (auto error = readNumber(*canary, "max_shift_pct", 0.0, policy.canary.maxShiftPct)) {
    return PolicyResult::Err("canary: " + *error);
  }
  if (policy.canary.maxShiftPct <= 0.0) return PolicyResult::Err("canary: 'max_shift_pct' must be positive");
  const Value* required = canary->find("required");
  if (required == nullptr || !required->isBool()) return PolicyResult::Err("canary: 'required' is missing or not a boolean");
  policy.canary.required = required->asBool();

  const Value* measured = document.find("measured");
  if (measured == nullptr || !measured->isObject()) return PolicyResult::Err("'measured' is missing");
  for (const auto& [name, target] :
       {std::pair<std::string_view, std::string*>{"study", &policy.measured.study},
        {"date", &policy.measured.date},
        {"profile", &policy.measured.profile}}) {
    if (const Value* value = measured->find(name)) {
      if (!value->isString()) return PolicyResult::Err("measured." + std::string(name) + " is not a string");
      *target = value->asString();
    }
  }
  return PolicyResult::Ok(std::move(policy));
}

PolicyResult loadPolicy(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return PolicyResult::Err("cannot read the policy file " + path);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto parsed = json::parse(text);
  if (parsed.isErr()) {
    return PolicyResult::Err("the policy file " + path + " is not valid JSON (offset " +
                             std::to_string(parsed.error().offset) + ")");
  }
  return parsePolicy(parsed.value());
}

bool matchesPattern(std::string_view pattern, std::string_view key) {
  // `*` matches any run of characters except '.', so the match recurses on the star only.
  if (pattern.empty()) return key.empty();
  if (pattern.front() == '*') {
    for (std::size_t consumed = 0;; ++consumed) {
      if (matchesPattern(pattern.substr(1), key.substr(consumed))) return true;
      if (consumed >= key.size() || key[consumed] == '.') return false;
    }
  }
  if (key.empty() || key.front() != pattern.front()) return false;
  return matchesPattern(pattern.substr(1), key.substr(1));
}

std::optional<Tier> gatedTier(const GatePolicy& policy, std::string_view key) {
  // A metric named in `reported` is never gated, even when a gated pattern also matches it: that is how
  // one metric of a wildcard (`passes.*.gpu_ms`) is demoted to report-only without listing the rest.
  if (anyMatch(policy.reported, key)) return std::nullopt;
  if (anyMatch(policy.gatedCounter, key)) return Tier::Counter;
  if (anyMatch(policy.gatedCpu, key)) return Tier::Cpu;
  if (anyMatch(policy.gatedGpu, key)) return Tier::Gpu;
  return std::nullopt;
}

bool isReported(const GatePolicy& policy, std::string_view key) { return anyMatch(policy.reported, key); }
bool isWorkload(const GatePolicy& policy, std::string_view key) { return anyMatch(policy.workload, key); }
bool isDispersionMetric(const GatePolicy& policy, std::string_view key) { return anyMatch(policy.dispersionMetrics, key); }

}  // namespace atlantis::runtime::gate

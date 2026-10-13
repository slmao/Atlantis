#include "gate_set.h"

#include <atlantis/profile_stats.h>

#include <algorithm>
#include <set>
#include <vector>

namespace atlantis::runtime::gate {

namespace profile = atlantis::profile;
using ResultRunValues = atlantis::Result<RunValues, std::string>;
using ResultValue = atlantis::Result<Value, std::string>;

namespace {

[[nodiscard]] bool isStats(const Value& value) { return value.isObject() && value.find("p50") != nullptr; }
[[nodiscard]] bool isUnavailable(const Value& value) {
  return value.isObject() && value.find("unavailable") != nullptr && value.find("p50") == nullptr;
}

void take(RunValues& out, const std::string& key, const Value& leaf) {
  if (isStats(leaf)) {
    const auto p50 = numberOf(leaf.find("p50"));
    const auto p95 = numberOf(leaf.find("p95"));
    if (p50) out.p50[key] = *p50;
    if (p95) out.p95[key] = *p95;
  } else if (isUnavailable(leaf)) {
    const Value* reason = leaf.find("unavailable");
    out.unavailable[key] = reason->isString() ? reason->asString() : std::string("unavailable");
  }
}

}  // namespace

Value pinValue(bool pinned) {
  Value out = Value::object();
  out.set("power_throttling", Value::string(pinned ? "disabled" : "default"));
  out.set("priority_class", Value::string(pinned ? "high" : "normal"));
  return out;
}

ResultRunValues extractRunValues(const Value& benchmarkDocument) {
  const Value* schema = benchmarkDocument.find("schema");
  if (schema == nullptr || !schema->isString() || schema->asString() != "atlantis.benchmark/1") {
    return ResultRunValues::Err("not an atlantis.benchmark/1 document");
  }
  const Value* metrics = benchmarkDocument.find("metrics");
  if (metrics == nullptr || !metrics->isObject()) return ResultRunValues::Err("the document has no metrics");

  RunValues out;
  for (const auto& [key, value] : metrics->asObject()) {
    if (key == "zones" && value.isObject()) {
      for (const auto& [zone, leaf] : value.asObject()) take(out, "zones." + zone, leaf);
    } else if (key == "passes" && value.isObject()) {
      for (const auto& [pass, fields] : value.asObject()) {
        if (!fields.isObject()) continue;
        for (const auto& [field, leaf] : fields.asObject()) take(out, "passes." + pass + "." + field, leaf);
      }
    } else {
      take(out, key, value);
    }
  }
  return ResultRunValues::Ok(std::move(out));
}

ResultValue buildSet(const Protocol& protocol, std::span<const Value> runDocuments) {
  if (runDocuments.empty()) return ResultValue::Err("a set needs at least one run");

  std::vector<RunValues> runs;
  runs.reserve(runDocuments.size());
  for (std::size_t i = 0; i < runDocuments.size(); ++i) {
    auto extracted = extractRunValues(runDocuments[i]);
    if (extracted.isErr()) return ResultValue::Err("run " + std::to_string(i + 1) + ": " + extracted.error());
    runs.push_back(std::move(extracted.value()));
  }
  for (const std::string_view section : {"meta", "scene", "settings"}) {
    const Value* first = runDocuments[0].find(section);
    for (std::size_t i = 1; i < runDocuments.size(); ++i) {
      const Value* other = runDocuments[i].find(section);
      const bool same = (first == nullptr && other == nullptr) || (first != nullptr && other != nullptr && *first == *other);
      if (!same) {
        return ResultValue::Err("run " + std::to_string(i + 1) + " differs from run 1 in " + std::string(section));
      }
    }
  }

  Value set = Value::object();
  set.set("schema", Value::string(std::string(kSetSchema)));

  Value protocolValue = Value::object();
  protocolValue.set("runs", count(protocol.runs));
  protocolValue.set("preheat_seconds", num(protocol.preheatSeconds));
  protocolValue.set("idle_wait", Value::boolean(protocol.idleWait));
  protocolValue.set("k", num(protocol.k));
  protocolValue.set("pin", pinValue(protocol.pin));
  protocolValue.set("power", Value::string(protocol.power));
  protocolValue.set("child_stderr", Value::string("discarded"));
  set.set("protocol", std::move(protocolValue));

  for (const std::string_view section : {"meta", "scene", "settings"}) {
    if (const Value* value = runDocuments[0].find(section)) set.set(std::string(section), *value);
  }

  Value runsValue = Value::array();
  for (const RunValues& run : runs) {
    Value entry = Value::object();
    Value values = Value::object();
    for (const auto& [key, v] : run.p50) values.set(key, num(v));
    Value p95 = Value::object();
    for (const auto& [key, v] : run.p95) p95.set(key, num(v));
    entry.set("values", std::move(values));
    entry.set("p95", std::move(p95));
    runsValue.push(std::move(entry));
  }
  set.set("runs", std::move(runsValue));

  // Every key any run mentioned, in sorted order.
  std::set<std::string> keys;
  for (const RunValues& run : runs) {
    for (const auto& [key, v] : run.p50) keys.insert(key);
    for (const auto& [key, reason] : run.unavailable) keys.insert(key);
  }
  Value aggregate = Value::object();
  for (const std::string& key : keys) {
    std::vector<double> values;
    std::string reason;
    for (const RunValues& run : runs) {
      if (const auto it = run.p50.find(key); it != run.p50.end()) {
        values.push_back(it->second);
      } else if (const auto u = run.unavailable.find(key); u != run.unavailable.end() && reason.empty()) {
        reason = u->second;
      }
    }
    Value entry = Value::object();
    if (values.size() == runs.size()) {
      entry.set("center", num(profile::median(values)));
      entry.set("sigma", num(profile::robustSigma(values)));
      entry.set("min", num(*std::min_element(values.begin(), values.end())));
      entry.set("max", num(*std::max_element(values.begin(), values.end())));
      entry.set("n", count(values.size()));
    } else {
      if (reason.empty()) {
        reason = "available in " + std::to_string(values.size()) + " of " + std::to_string(runs.size()) + " runs";
      }
      entry.set("unavailable", Value::string(reason));
    }
    aggregate.set(key, std::move(entry));
  }
  set.set("aggregate", std::move(aggregate));
  return ResultValue::Ok(std::move(set));
}

}  // namespace atlantis::runtime::gate

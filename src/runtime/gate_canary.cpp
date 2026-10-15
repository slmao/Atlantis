#include "gate_canary.h"

#include <atlantis/profile_stats.h>

#include <algorithm>
#include <cmath>

namespace atlantis::runtime::gate {

namespace profile = atlantis::profile;
using ReadingResult = atlantis::Result<CanaryReading, std::string>;

namespace {

[[nodiscard]] Value statsValue(const std::vector<double>& samples) {
  Value out = Value::object();
  out.set("p50", num(profile::median(samples)));
  out.set("min", num(*std::min_element(samples.begin(), samples.end())));
  out.set("max", num(*std::max_element(samples.begin(), samples.end())));
  out.set("n", count(samples.size()));
  return out;
}

}  // namespace

Value buildCanaryDocument(Value meta, const std::array<std::vector<double>, kCanaryComponentCount>& repetitionMs) {
  Value doc = Value::object();
  doc.set("schema", Value::string(std::string(kCanarySchema)));
  doc.set("meta", std::move(meta));
  Value workload = Value::object();
  workload.set("id", Value::string(std::string(kCanaryWorkloadId)));
  Value extent = Value::object();
  extent.set("width", count(kCanaryWidth));
  extent.set("height", count(kCanaryHeight));
  workload.set("extent", std::move(extent));
  workload.set("repetitions", count(kCanaryRepetitions));
  workload.set("warmup", count(kCanaryWarmup));
  workload.set("alu_iterations", count(kCanaryAluIterations));
  workload.set("bw_overdraw", count(kCanaryBwOverdraw));
  workload.set("tex_fetches", count(kCanaryTexFetches));
  doc.set("workload", std::move(workload));
  Value components = Value::object();
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    if (!repetitionMs[c].empty()) components.set(std::string(kCanaryComponents[c]), statsValue(repetitionMs[c]));
  }
  doc.set("components", std::move(components));
  return doc;
}

ReadingResult extractCanaryReading(const Value& document) {
  const Value* schema = document.find("schema");
  if (schema == nullptr || !schema->isString() || schema->asString() != kCanarySchema) {
    return ReadingResult::Err("not an atlantis.benchmark.canary/1 document");
  }
  const Value* id = at(document, {"workload", "id"});
  if (id == nullptr || !id->isString() || id->asString() != kCanaryWorkloadId) {
    return ReadingResult::Err("the canary workload is not " + std::string(kCanaryWorkloadId));
  }
  CanaryReading reading;
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    const auto value = numberOf(at(document, {"components", kCanaryComponents[c], "p50"}));
    if (!value || !(*value > 0.0)) {
      return ReadingResult::Err("the canary has no reading for component " + std::string(kCanaryComponents[c]));
    }
    reading.ms[c] = *value;
  }
  return ReadingResult::Ok(reading);
}

const Value* canarySystem(const Value& document) { return at(document, {"meta", "system"}); }

Value canarySetValue(const std::vector<std::pair<std::string, CanaryReading>>& readings) {
  Value out = Value::object();
  out.set("workload", Value::string(std::string(kCanaryWorkloadId)));
  Value measurements = Value::array();
  for (const auto& [phase, reading] : readings) {
    Value m = Value::object();
    m.set("phase", Value::string(phase));
    for (std::size_t c = 0; c < kCanaryComponentCount; ++c) m.set(std::string(kCanaryComponents[c]), num(reading.ms[c]));
    measurements.push(std::move(m));
  }
  out.set("measurements", std::move(measurements));
  Value aggregate = Value::object();
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    std::vector<double> samples;
    for (const auto& entry : readings) samples.push_back(entry.second.ms[c]);
    if (samples.empty()) continue;
    Value stats = Value::object();
    stats.set("center", num(profile::median(samples)));
    stats.set("sigma", num(profile::robustSigma(samples)));
    stats.set("min", num(*std::min_element(samples.begin(), samples.end())));
    stats.set("max", num(*std::max_element(samples.begin(), samples.end())));
    stats.set("n", count(samples.size()));
    aggregate.set(std::string(kCanaryComponents[c]), std::move(stats));
  }
  out.set("aggregate", std::move(aggregate));
  return out;
}

CanaryJudgment judgeCanary(const Value& candidate, const Value& baseline, double maxShiftPct) {
  CanaryJudgment out;
  out.thresholdPct = maxShiftPct;
  const Value* baseWorkload = at(baseline, {"canary", "workload"});
  const Value* candWorkload = at(candidate, {"canary", "workload"});
  if (baseWorkload == nullptr || !baseWorkload->isString()) {
    out.problem = "the baseline has no GPU canary reference: record it again with this build";
    return out;
  }
  if (candWorkload == nullptr || !candWorkload->isString()) {
    out.problem = "this run has no GPU canary measurement";
    return out;
  }
  if (baseWorkload->asString() != candWorkload->asString()) {
    out.problem = "the baseline's GPU canary workload (" + baseWorkload->asString() + ") is not this build's (" +
                  candWorkload->asString() + "): record the baseline again";
    return out;
  }
  const Value* measurements = at(candidate, {"canary", "measurements"});
  if (measurements == nullptr || !measurements->isArray() || measurements->asArray().empty()) {
    out.problem = "this run has no GPU canary measurement";
    return out;
  }
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    const auto center = numberOf(at(baseline, {"canary", "aggregate", kCanaryComponents[c], "center"}));
    const auto sigma = numberOf(at(baseline, {"canary", "aggregate", kCanaryComponents[c], "sigma"}));
    if (!center || !(*center > 0.0)) {
      out.problem = "the baseline's GPU canary has no reference for component " + std::string(kCanaryComponents[c]);
      return out;
    }
    out.reference[c] = *center;
    out.referenceSigma[c] = sigma.value_or(0.0);
    double worst = 0.0;
    for (const Value& m : measurements->asArray()) {
      const auto value = numberOf(m.find(std::string(kCanaryComponents[c])));
      if (!value) {
        out.problem = "a GPU canary measurement lacks component " + std::string(kCanaryComponents[c]);
        return out;
      }
      worst = std::max(worst, *value);
    }
    out.worst[c] = worst;
    out.shiftPct[c] = (worst / *center - 1.0) * 100.0;
    if (out.shiftPct[c] > maxShiftPct) {
      out.shifted = true;
      out.shiftedComponents.emplace_back(kCanaryComponents[c]);
    }
  }
  out.measured = true;
  return out;
}

std::size_t canaryComponentIndex(std::string_view name) noexcept {
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    if (kCanaryComponents[c] == name) return c;
  }
  return 0;
}

Value canaryJudgmentValue(const CanaryJudgment& judgment) {
  Value out = Value::object();
  out.set("workload", Value::string(std::string(kCanaryWorkloadId)));
  out.set("measured", Value::boolean(judgment.measured));
  out.set("threshold_pct", num(judgment.thresholdPct));
  if (!judgment.measured) {
    out.set("shifted", Value());
    out.set("problem", Value::string(judgment.problem));
    return out;
  }
  Value reference = Value::object();
  Value candidate = Value::object();
  Value shift = Value::object();
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    const std::string name(kCanaryComponents[c]);
    Value ref = Value::object();
    ref.set("center", num(judgment.reference[c]));
    ref.set("sigma", num(judgment.referenceSigma[c]));
    reference.set(name, std::move(ref));
    candidate.set(name, num(judgment.worst[c]));
    shift.set(name, num(judgment.shiftPct[c]));
  }
  out.set("reference", std::move(reference));
  out.set("candidate", std::move(candidate));
  out.set("shift_pct", std::move(shift));
  out.set("shifted", Value::boolean(judgment.shifted));
  Value shifted = Value::array();
  for (const std::string& name : judgment.shiftedComponents) shifted.push(Value::string(name));
  out.set("shifted_components", std::move(shifted));
  return out;
}

double canarySpreadPct(const Value& set) {
  double worst = -1.0;
  for (std::size_t c = 0; c < kCanaryComponentCount; ++c) {
    const auto lo = numberOf(at(set, {"canary", "aggregate", kCanaryComponents[c], "min"}));
    const auto hi = numberOf(at(set, {"canary", "aggregate", kCanaryComponents[c], "max"}));
    if (!lo || !hi || !(*lo > 0.0)) return -1.0;
    worst = std::max(worst, (*hi / *lo - 1.0) * 100.0);
  }
  return worst;
}

}  // namespace atlantis::runtime::gate

#pragma once

// Spec 0060 (Plan 0060 M4-M5): the fakes the gate's orchestration and driver tests share -- a clock
// that moves only when told to sleep, a CPU sampler that reports scripted idle percentages, and a
// child launcher that records its launches and writes the benchmark document a test hands it.

#include "../../src/runtime/gate_orchestrate.h"

#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gate_test {

class FakeClock final : public gate::Clock {
 public:
  double nowSeconds() override { return now; }
  void sleepSeconds(double seconds) override {
    now += seconds;
    slept += seconds;
    ++sleeps;
  }
  std::string isoUtc() override { return "2026-10-12T00:00:00Z"; }
  double now = 0.0;
  double slept = 0.0;
  int sleeps = 0;
};

// One interval per read(): the idle percentage it reports, in order; the last repeats.
class FakeSampler final : public gate::CpuSampler {
 public:
  explicit FakeSampler(std::vector<double> idlePercents) : idle(std::move(idlePercents)) {}
  std::optional<gate::CpuTimes> read() override {
    ++reads;
    if (unavailable) return std::nullopt;
    if (reads > 1) {
      const double pct = idle[std::min(idle.size() - 1, static_cast<std::size_t>(reads - 2))];
      times.total += 1000;
      times.idle += static_cast<std::uint64_t>(pct * 10.0);
    }
    return times;
  }
  std::vector<double> idle;
  bool unavailable = false;
  int reads = 0;
  gate::CpuTimes times;
};

// The power source per read(): the sources it reports, in order; the last repeats.
class FakePower final : public gate::PowerProbe {
 public:
  explicit FakePower(std::vector<gate::PowerSource> sequence = {gate::PowerSource::Mains})
      : sources(std::move(sequence)) {}
  gate::PowerSource read() override {
    const gate::PowerSource source = sources[std::min(sources.size() - 1, reads)];
    ++reads;
    return source;
  }
  std::vector<gate::PowerSource> sources;
  std::size_t reads = 0;
};

// Records every launch; writes the document `makeDocument(callIndex)` to the child's --output
// unless told not to (call 0 is the preheat when the plan has one). A `--canary` child writes the canary
// document `canaryMs(phase)` says instead and does not count in the benchmark children's call index, so
// `makeDocument` and `scripted` mean the same with or without a canary in the plan; `scriptedCanary` is keyed by
// the canary children's own call index.
class FakeLauncher final : public gate::ChildLauncher {
 public:
  gate::ChildResult run(const gate::ChildSpec& spec) override {
    calls.push_back(spec);
    if (!spec.args.empty() && spec.args[0] == "--canary") return runCanary(spec);
    const std::size_t index = benchmarkCalls++;
    gate::ChildResult result;
    if (const auto it = scripted.find(index); it != scripted.end()) result = it->second;
    if (result.status == gate::ChildResult::Status::Exited && result.exitCode == 0 && writeDocuments) {
      for (std::size_t i = 0; i + 1 < spec.args.size(); ++i) {
        if (spec.args[i] != "--output") continue;
        std::ofstream out(spec.args[i + 1], std::ios::binary);
        if (malformed) {
          out << "{not json";
        } else if (makeDocument) {
          out << atlantis::connection::json::write(makeDocument(index)) << "\n";
        } else {
          out << atlantis::connection::json::write(makeRun({{"cpu_active_ms", 12.0 + static_cast<double>(index)}}))
              << "\n";
        }
      }
    }
    return result;
  }
  [[nodiscard]] std::size_t canaryCallCount() const { return canaryCalls; }
  [[nodiscard]] std::size_t benchmarkCallCount() const { return benchmarkCalls; }

  std::vector<gate::ChildSpec> calls;
  std::map<std::size_t, gate::ChildResult> scripted;
  std::map<std::size_t, gate::ChildResult> scriptedCanary;
  // The reading the canary child reports, by phase ("start" or "end"; from its output file's name).
  std::function<CanaryMs(const std::string&)> canaryMs = [](const std::string&) { return kStableCanary; };
  DocumentIdentity canaryIdentity;
  std::function<Value(std::size_t)> makeDocument;
  bool writeDocuments = true;
  bool malformed = false;

 private:
  gate::ChildResult runCanary(const gate::ChildSpec& spec) {
    const std::size_t index = canaryCalls++;
    gate::ChildResult result;
    if (const auto it = scriptedCanary.find(index); it != scriptedCanary.end()) result = it->second;
    if (result.status != gate::ChildResult::Status::Exited || result.exitCode != 0 || !writeDocuments) return result;
    for (std::size_t i = 0; i + 1 < spec.args.size(); ++i) {
      if (spec.args[i] != "--output") continue;
      const std::string& path = spec.args[i + 1];
      const std::string phase = path.find("canary_end") != std::string::npos ? "end" : "start";
      std::ofstream out(path, std::ios::binary);
      if (malformed) {
        out << "{not json";
      } else {
        out << atlantis::connection::json::write(makeCanaryDocument(canaryMs(phase), canaryIdentity)) << "\n";
      }
    }
    return result;
  }

  std::size_t benchmarkCalls = 0;
  std::size_t canaryCalls = 0;
};

}  // namespace gate_test

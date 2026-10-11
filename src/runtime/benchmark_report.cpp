#include "benchmark_report.h"

#include <atlantis/profile_stats.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <utility>

namespace atlantis::runtime::benchmark {

namespace json = atlantis::connection::json;
namespace profile = atlantis::profile;
using json::Value;

namespace {

constexpr double kNsPerMs = 1.0e6;

[[nodiscard]] std::string formatNumber(double value) {
  if (!std::isfinite(value)) value = 0.0;
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), result.ptr);
}

[[nodiscard]] Value num(double value) { return Value::numberLiteral(formatNumber(value)); }
[[nodiscard]] Value count(std::size_t value) { return Value::number(static_cast<std::uint64_t>(value)); }

[[nodiscard]] Value unavailable(std::string_view reason) {
  Value out = Value::object();
  out.set("unavailable", Value::string(std::string(reason)));
  return out;
}

[[nodiscard]] Value statsValue(const profile::Stats& stats) {
  Value out = Value::object();
  out.set("avg", num(stats.avg));
  out.set("p50", num(stats.p50));
  out.set("p95", num(stats.p95));
  out.set("p99", num(stats.p99));
  out.set("min", num(stats.min));
  out.set("max", num(stats.max));
  out.set("n", count(stats.n));
  return out;
}

// A metric's samples over the measured frames, or why there are none.
struct Series {
  std::vector<double> values;
  std::string reason;  // used when values is empty
};

[[nodiscard]] Value seriesStats(const Series& series) {
  if (series.values.empty()) return unavailable(series.reason.empty() ? "no samples" : series.reason);
  return statsValue(profile::computeStats(series.values));
}

[[nodiscard]] Value seriesArray(const std::vector<double>& values) {
  Value out = Value::array();
  for (const double v : values) out.push(num(v));
  return out;
}

// The accumulator/gauge keys of the counter set that are plain per-frame numbers;
// a frame that never wrote one has none of that event, so it samples 0.
constexpr std::array<std::string_view, 17> kCounterKeys = {
    "draw_calls",       "triangles",       "renderables_total", "renderables_drawn", "renderables_culled",
    "lights_directional", "lights_point",  "pipeline_binds",    "descriptor_set_binds", "descriptor_writes",
    "vertex_buffer_binds", "index_buffer_binds", "push_constants", "upload_bytes",      "uniform_write_bytes",
    "materials_realized", "renderables_skipped"};

// Hardware-dependent counters: unavailable when no frame produced them.
constexpr std::array<std::string_view, 2> kOptionalKeys = {"fragment_invocations", "ia_primitives"};

[[nodiscard]] double zoneMs(const profile::FrameRecord& record, profile::NameId id) {
  return id < profile::kMaxNames ? static_cast<double>(record.zoneNs[id]) / kNsPerMs : 0.0;
}

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

struct Collected {
  std::vector<std::pair<std::string, Series>> flat;  // in schema order
  std::map<std::string, Series> zones;
  std::map<std::string, std::pair<Series, Series>> passes;  // gpu_ms, cpu_record_ms
};

void add(Collected& out, std::string key, Series series) { out.flat.emplace_back(std::move(key), std::move(series)); }

[[nodiscard]] std::string gpuReason(const ReportInput& input, bool anyDelivered) {
  if (!input.settings.gpuTiming) return "disabled by --gpu-timing=off";
  if (!input.device.timestampsSupported) return "this device cannot write timestamps on its graphics queue";
  if (!anyDelivered) return "no GPU results were delivered for the measured frames";
  return "no samples";
}

[[nodiscard]] Collected collect(const ReportInput& input) {
  Collected out;
  const std::vector<MeasuredFrame>& frames = input.frames;

  const profile::NameId frameZone = profile::internName("Frame");
  const profile::NameId waitPrev = profile::internName("WaitPreviousGpu");
  const profile::NameId acquire = profile::internName("Acquire");
  const profile::NameId present = profile::internName("Present");
  const profile::NameId waitIdle = profile::internName("WaitIdle");

  Series frameMs, cpuMs, cpuActive, waitPrevMs, acquireMs, presentMs, waitIdleMs;
  for (const MeasuredFrame& f : frames) {
    const double cpu = zoneMs(f.record, frameZone);
    frameMs.values.push_back(f.frameMs);
    cpuMs.values.push_back(cpu);
    // WaitPreviousGpu is the previous submission's fence wait, which runs at the top of
    // acquireNextTarget(): it is nested in the Acquire zone, so the two blocking waits
    // that are disjoint are Acquire (inclusive) and WaitIdle, and acquire_ms reports
    // what is left of Acquire once the fence wait is taken out.
    const double fenceWait = zoneMs(f.record, waitPrev);
    cpuActive.values.push_back(cpu - (zoneMs(f.record, acquire) + zoneMs(f.record, waitIdle)));
    waitPrevMs.values.push_back(fenceWait);
    acquireMs.values.push_back(std::max(0.0, zoneMs(f.record, acquire) - fenceWait));
    presentMs.values.push_back(zoneMs(f.record, present));
    waitIdleMs.values.push_back(zoneMs(f.record, waitIdle));
  }

  // GPU: top-level regions are the passes; the span is first begin to last end.
  bool anyDelivered = false;
  Series gpuBusy, gpuSpan;
  std::map<std::string, Series> passGpu;
  Series bloomGpu;
  for (const MeasuredFrame& f : frames) {
    const profile::FrameRecord& r = f.record;
    if (!r.gpuDelivered || r.regionCount == 0) continue;
    anyDelivered = true;
    double busy = 0.0;
    std::uint64_t begin = ~std::uint64_t{0};
    std::uint64_t end = 0;
    std::map<std::string, double> perName;
    double bloom = 0.0;
    bool hasBloom = false;
    for (std::uint32_t i = 0; i < r.regionCount; ++i) {
      const profile::GpuRegion& region = r.regions[i];
      const double duration = static_cast<double>(region.endNs - region.beginNs) / kNsPerMs;
      begin = std::min(begin, region.beginNs);
      end = std::max(end, region.endNs);
      const std::string name(profile::nameOf(region.name));
      perName[name] += duration;
      if (region.depth == 0) {
        busy += duration;
        if (startsWith(name, "bloom_")) {
          bloom += duration;
          hasBloom = true;
        }
      }
    }
    gpuBusy.values.push_back(busy);
    gpuSpan.values.push_back(static_cast<double>(end - begin) / kNsPerMs);
    for (const auto& [name, duration] : perName) passGpu[name].values.push_back(duration);
    if (hasBloom) bloomGpu.values.push_back(bloom);
  }
  const std::string reasonGpu = gpuReason(input, anyDelivered);
  gpuBusy.reason = gpuSpan.reason = reasonGpu;

  add(out, "frame_ms", std::move(frameMs));
  add(out, "cpu_ms", std::move(cpuMs));
  add(out, "cpu_active_ms", std::move(cpuActive));
  add(out, "gpu_busy_ms", std::move(gpuBusy));
  add(out, "gpu_span_ms", std::move(gpuSpan));

  for (const std::string_view key : kCounterKeys) {
    Series series;
    const profile::NameId id = profile::internName(key);
    const profile::NameId total = profile::internName("renderables_total");
    const profile::NameId drawn = profile::internName("renderables_drawn");
    for (const MeasuredFrame& f : frames) {
      const profile::FrameRecord& r = f.record;
      if (key == "renderables_skipped") {
        series.values.push_back(r.counterSet.test(total) && r.counterSet.test(drawn) ? r.counter[total] - r.counter[drawn]
                                                                                       : 0.0);
      } else {
        series.values.push_back(id < profile::kMaxNames && r.counterSet.test(id) ? r.counter[id] : 0.0);
      }
    }
    add(out, std::string(key), std::move(series));
  }

  add(out, "wait_previous_gpu_ms", std::move(waitPrevMs));
  add(out, "acquire_ms", std::move(acquireMs));
  add(out, "present_ms", std::move(presentMs));
  add(out, "wait_idle_ms", std::move(waitIdleMs));

  for (const std::string_view key : kOptionalKeys) {
    Series series;
    series.reason = "the device does not support pipeline statistics queries, or the draw pass was not timed";
    const profile::NameId id = profile::internName(key);
    for (const MeasuredFrame& f : frames) {
      if (id < profile::kMaxNames && f.record.counterSet.test(id)) series.values.push_back(f.record.counter[id]);
    }
    add(out, std::string(key), std::move(series));
  }

  // Zones: every name that ended as a zone in any measured frame.
  for (std::size_t id = 0; id < profile::nameCount(); ++id) {
    bool seen = false;
    for (const MeasuredFrame& f : frames) {
      if (f.record.zoneCount[id] > 0) {
        seen = true;
        break;
      }
    }
    if (!seen) continue;
    Series series;
    for (const MeasuredFrame& f : frames) series.values.push_back(zoneMs(f.record, static_cast<profile::NameId>(id)));
    out.zones.emplace(std::string(profile::nameOf(static_cast<profile::NameId>(id))), std::move(series));
  }

  // Passes: GPU time per region name, CPU recording time per pass label.
  constexpr std::string_view kRecordPrefix = "RecordPass:";
  Series bloomCpu;
  bool bloomCpuSeen = false;
  for (auto& [name, series] : passGpu) {
    series.reason = reasonGpu;
    out.passes[name].first = std::move(series);
  }
  for (const auto& [zoneName, series] : out.zones) {
    if (!startsWith(zoneName, kRecordPrefix)) continue;
    out.passes[zoneName.substr(kRecordPrefix.size())].second = series;
  }
  for (const MeasuredFrame& f : frames) {
    double sum = 0.0;
    bool any = false;
    for (std::size_t id = 0; id < profile::nameCount(); ++id) {
      const std::string_view zoneName = profile::nameOf(static_cast<profile::NameId>(id));
      if (startsWith(zoneName, "RecordPass:bloom_") && f.record.zoneCount[id] > 0) {
        sum += zoneMs(f.record, static_cast<profile::NameId>(id));
        any = true;
      }
    }
    if (any) {
      bloomCpu.values.push_back(sum);
      bloomCpuSeen = true;
    }
  }
  if (!bloomGpu.values.empty() || bloomCpuSeen) {
    bloomGpu.reason = reasonGpu;
    out.passes["Bloom"] = {std::move(bloomGpu), std::move(bloomCpu)};  // derived: the sum of the bloom_* passes
  }
  for (auto& [name, pair] : out.passes) {
    if (pair.first.values.empty() && pair.first.reason.empty()) pair.first.reason = "this region is not a GPU pass";
    if (pair.second.values.empty()) pair.second.reason = "this region has no CPU recording zone";
  }
  return out;
}

[[nodiscard]] Value optionalString(const std::string& text) {
  return text.empty() ? Value() : Value::string(text);
}

}  // namespace

Value buildDocument(const ReportInput& input) {
  const Collected collected = collect(input);

  Value doc = Value::object();
  doc.set("schema", Value::string(std::string(kSchema)));

  Value build = Value::object();
  build.set("type", Value::string(input.build.type));
  build.set("profiling", Value::boolean(input.build.profiling));
  build.set("tracy", Value::boolean(input.build.tracy));
  build.set("gpu_markers", Value::boolean(input.build.gpuMarkers));
  build.set("validation", Value::boolean(input.build.validation));
  build.set("compiler", Value::string(input.build.compiler));
  build.set("git_revision", optionalString(input.build.gitRevision));
  build.set("git_dirty", Value::boolean(input.build.gitDirty));
  Value system = Value::object();
  system.set("os", Value::string(input.system.os));
  system.set("cpu", optionalString(input.system.cpu));
  system.set("gpu", optionalString(input.device.gpu));
  system.set("driver", optionalString(input.device.driver));
  system.set("vulkan_api", optionalString(input.device.vulkanApi));
  system.set("timestamp_period_ns", input.device.timestampsSupported ? num(input.device.timestampPeriodNs) : Value());
  Value display = Value::object();
  display.set("present_mode", optionalString(input.device.presentMode));
  display.set("refresh_hz", input.device.refreshHz > 0.0 ? num(input.device.refreshHz) : Value());
  Value meta = Value::object();
  meta.set("build", std::move(build));
  meta.set("system", std::move(system));
  meta.set("display", std::move(display));
  doc.set("meta", std::move(meta));

  Value scene = Value::object();
  scene.set("name", Value::string(input.scene.name));
  scene.set("guid", Value::string(input.scene.guid));
  scene.set("content_pin", optionalString(input.scene.contentPin));
  doc.set("scene", std::move(scene));

  Value resolution = Value::object();
  resolution.set("width", count(input.settings.width));
  resolution.set("height", count(input.settings.height));
  Value settings = Value::object();
  settings.set("resolution", std::move(resolution));
  settings.set("warmup", count(input.settings.warmup));
  settings.set("frames", count(input.settings.frames));
  settings.set("variant", Value::string(input.settings.variant));
  settings.set("gpu_timing", Value::boolean(input.settings.gpuTiming));
  doc.set("settings", std::move(settings));

  Value frames = Value::object();
  frames.set("measured", count(input.frames.size()));
  frames.set("discarded", count(input.discardedFrames));
  frames.set("skipped", count(input.skippedFrames));
  doc.set("frames", std::move(frames));

  Value metrics = Value::object();
  for (const auto& [key, series] : collected.flat) metrics.set(key, seriesStats(series));
  Value zones = Value::object();
  for (const auto& [name, series] : collected.zones) zones.set(name, seriesStats(series));
  metrics.set("zones", std::move(zones));
  Value passes = Value::object();
  for (const auto& [name, pair] : collected.passes) {
    Value pass = Value::object();
    pass.set("gpu_ms", seriesStats(pair.first));
    pass.set("cpu_record_ms", seriesStats(pair.second));
    passes.set(name, std::move(pass));
  }
  metrics.set("passes", std::move(passes));
  doc.set("metrics", std::move(metrics));

  if (input.includeSeries) {
    Value series = Value::object();
    for (const auto& [key, s] : collected.flat) {
      if (!s.values.empty()) series.set(key, seriesArray(s.values));
    }
    doc.set("series", std::move(series));
  }
  return doc;
}

namespace {

using Path = std::vector<std::string_view>;

[[nodiscard]] const Value* at(const Value& root, const Path& path) {
  const Value* current = &root;
  for (const std::string_view key : path) {
    if (current == nullptr) return nullptr;
    current = current->find(key);
  }
  return current;
}

[[nodiscard]] std::optional<double> numberOf(const Value* value) {
  if (value == nullptr || !value->isNumber()) return std::nullopt;
  const std::string& text = value->numberText();
  double out = 0.0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  if (result.ec != std::errc{}) return std::nullopt;
  return out;
}

[[nodiscard]] std::string describe(const Value* value) {
  return value == nullptr ? std::string("<absent>") : json::write(*value);
}

// metric path -> its stats object (skipping unavailable ones)
void gatherStats(const Value& metrics, std::map<std::string, const Value*>& out) {
  const auto isStats = [](const Value& v) { return v.isObject() && v.find("p50") != nullptr; };
  for (const auto& [key, value] : metrics.asObject()) {
    if (key == "zones" && value.isObject()) {
      for (const auto& [zone, stats] : value.asObject()) {
        if (isStats(stats)) out.emplace("zones." + zone, &stats);
      }
    } else if (key == "passes" && value.isObject()) {
      for (const auto& [pass, fields] : value.asObject()) {
        if (!fields.isObject()) continue;
        for (const auto& [field, stats] : fields.asObject()) {
          if (isStats(stats)) out.emplace("passes." + pass + "." + field, &stats);
        }
      }
    } else if (isStats(value)) {
      out.emplace(key, &value);
    }
  }
}

[[nodiscard]] Value change(const Value& baseline, const Value& current, std::string_view field) {
  const std::optional<double> b = numberOf(baseline.find(field));
  const std::optional<double> c = numberOf(current.find(field));
  Value out = Value::object();
  out.set("baseline", b ? num(*b) : Value());
  out.set("current", c ? num(*c) : Value());
  if (b && c) {
    out.set("delta", num(*c - *b));
    out.set("delta_pct", *b != 0.0 ? num((*c - *b) / *b * 100.0) : Value());
  }
  return out;
}

}  // namespace

Value compareDocuments(const Value& baseline, const Value& current) {
  struct Field {
    const char* name;
    Path path;
  };
  const std::array<Field, 13> fields = {{
      {"schema", {"schema"}},
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
      {"meta.system.gpu", {"meta", "system", "gpu"}},
  }};
  Value reasons = Value::array();
  const auto check = [&](const char* name, const Path& path) {
    const Value* b = at(baseline, path);
    const Value* c = at(current, path);
    const bool bothAbsent = b == nullptr && c == nullptr;
    if (!bothAbsent && (b == nullptr || c == nullptr || !(*b == *c))) {
      reasons.push(Value::string(std::string(name) + " differs: baseline " + describe(b) + ", current " + describe(c)));
    }
  };
  for (const Field& field : fields) check(field.name, field.path);
  check("meta.system.driver", {"meta", "system", "driver"});

  Value out = Value::object();
  out.set("comparable", Value::boolean(reasons.asArray().empty()));
  out.set("reasons", std::move(reasons));

  Value metricsOut = Value::object();
  const Value* baseMetrics = baseline.find("metrics");
  const Value* currentMetrics = current.find("metrics");
  if (baseMetrics != nullptr && currentMetrics != nullptr && baseMetrics->isObject() && currentMetrics->isObject()) {
    std::map<std::string, const Value*> a;
    std::map<std::string, const Value*> b;
    gatherStats(*baseMetrics, a);
    gatherStats(*currentMetrics, b);
    for (const auto& [path, baseStats] : a) {
      const auto it = b.find(path);
      if (it == b.end()) continue;
      const Value& currentStats = *it->second;
      Value entry = Value::object();
      for (const std::string_view field : {"avg", "p50", "p95", "p99"}) {
        entry.set(std::string(field), change(*baseStats, currentStats, field));
      }
      const auto noise = [](const Value& stats) -> Value {
        const auto p95 = numberOf(stats.find("p95"));
        const auto p50 = numberOf(stats.find("p50"));
        return p95 && p50 ? num(*p95 - *p50) : Value();
      };
      Value spread = Value::object();
      spread.set("baseline", noise(*baseStats));
      spread.set("current", noise(currentStats));
      entry.set("noise", std::move(spread));
      metricsOut.set(path, std::move(entry));
    }
  }
  out.set("metrics", std::move(metricsOut));
  return out;
}

std::string summarize(const Value& document) {
  std::string text;
  const auto str = [&](const Path& path) {
    const Value* v = at(document, path);
    return v != nullptr && v->isString() ? v->asString() : std::string("?");
  };
  text += std::format("{}  scene {}  variant {}\n", str({"schema"}), str({"scene", "name"}), str({"settings", "variant"}));
  const auto width = numberOf(at(document, {"settings", "resolution", "width"}));
  const auto height = numberOf(at(document, {"settings", "resolution", "height"}));
  const auto measured = numberOf(at(document, {"frames", "measured"}));
  text += std::format("{:.0f}x{:.0f}, {:.0f} measured frames\n", width.value_or(0), height.value_or(0), measured.value_or(0));
  const Value* metrics = document.find("metrics");
  for (const std::string_view key : {"frame_ms", "cpu_active_ms", "gpu_busy_ms", "wait_previous_gpu_ms", "draw_calls", "triangles"}) {
    const Value* stats = metrics != nullptr ? metrics->find(key) : nullptr;
    if (stats == nullptr || stats->find("p50") == nullptr) {
      text += std::format("{:<22} unavailable\n", key);
      continue;
    }
    text += std::format("{:<22} avg {:9.3f}  p50 {:9.3f}  p95 {:9.3f}  p99 {:9.3f}\n", key,
                        numberOf(stats->find("avg")).value_or(0), numberOf(stats->find("p50")).value_or(0),
                        numberOf(stats->find("p95")).value_or(0), numberOf(stats->find("p99")).value_or(0));
  }
  return text;
}

}  // namespace atlantis::runtime::benchmark

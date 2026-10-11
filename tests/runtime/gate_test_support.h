#pragma once

// Spec 0060 (Plan 0060 M1-M2): synthetic `atlantis.benchmark/1` documents for the
// gate's GPU-free tests -- exactly the members the gate reads, with every metric a
// stats object whose p50 is the given value. No device, no clock.

#include "../../src/runtime/gate_json.h"

#include <map>
#include <string>

namespace gate_test {

namespace gate = atlantis::runtime::gate;
using gate::Value;

struct DocumentIdentity {
  std::string scene = "bistro";
  std::string guid = "guid-1";
  std::string contentPin = "pin-1";
  unsigned int width = 1920;
  unsigned int height = 1080;
  std::string variant = "baseline";
  std::string buildType = "Release";
  bool validation = false;
  std::string os = "windows";
  std::string cpu = "Test CPU";
  std::string gpu = "Test GPU";
  std::string driver = "1.0";
  std::string vulkanApi = "1.4.0";
  std::string presentMode = "FIFO";
  unsigned int refreshHz = 60;
};

[[nodiscard]] inline Value statsLeaf(double p50) {
  Value out = Value::object();
  out.set("avg", gate::num(p50));
  out.set("p50", gate::num(p50));
  out.set("p95", gate::num(p50 * 1.1));
  out.set("p99", gate::num(p50 * 1.2));
  out.set("min", gate::num(p50 * 0.9));
  out.set("max", gate::num(p50 * 1.3));
  out.set("n", gate::count(300));
  return out;
}

[[nodiscard]] inline Value unavailableLeaf(const std::string& reason) {
  Value out = Value::object();
  out.set("unavailable", Value::string(reason));
  return out;
}

// `p50s` is keyed as the gate keys metrics: `cpu_active_ms`, `zones.<Zone>`,
// `passes.<label>.<field>`. A key in `unavailable` becomes an unavailable leaf.
[[nodiscard]] inline Value makeRun(const std::map<std::string, double>& p50s, const DocumentIdentity& id = {},
                                   const std::map<std::string, std::string>& unavailable = {}) {
  Value doc = Value::object();
  doc.set("schema", Value::string("atlantis.benchmark/1"));

  Value build = Value::object();
  build.set("type", Value::string(id.buildType));
  build.set("profiling", Value::boolean(true));
  build.set("tracy", Value::boolean(false));
  build.set("gpu_markers", Value::boolean(true));
  build.set("validation", Value::boolean(id.validation));
  Value system = Value::object();
  system.set("os", Value::string(id.os));
  system.set("cpu", Value::string(id.cpu));
  system.set("gpu", Value::string(id.gpu));
  system.set("driver", Value::string(id.driver));
  system.set("vulkan_api", Value::string(id.vulkanApi));
  Value display = Value::object();
  display.set("present_mode", Value::string(id.presentMode));
  display.set("refresh_hz", Value::number(static_cast<std::uint64_t>(id.refreshHz)));
  Value meta = Value::object();
  meta.set("build", std::move(build));
  meta.set("system", std::move(system));
  meta.set("display", std::move(display));
  doc.set("meta", std::move(meta));

  Value scene = Value::object();
  scene.set("name", Value::string(id.scene));
  scene.set("guid", Value::string(id.guid));
  scene.set("content_pin", Value::string(id.contentPin));
  doc.set("scene", std::move(scene));

  Value resolution = Value::object();
  resolution.set("width", Value::number(static_cast<std::uint64_t>(id.width)));
  resolution.set("height", Value::number(static_cast<std::uint64_t>(id.height)));
  Value settings = Value::object();
  settings.set("resolution", std::move(resolution));
  settings.set("warmup", Value::number(std::uint64_t{120}));
  settings.set("frames", Value::number(std::uint64_t{300}));
  settings.set("variant", Value::string(id.variant));
  settings.set("gpu_timing", Value::boolean(true));
  doc.set("settings", std::move(settings));

  Value metrics = Value::object();
  Value zones = Value::object();
  Value passes = Value::object();
  std::map<std::string, Value> passMap;
  const auto place = [&](const std::string& key, Value leaf) {
    if (key.rfind("zones.", 0) == 0) {
      zones.set(key.substr(6), std::move(leaf));
    } else if (key.rfind("passes.", 0) == 0) {
      const std::string rest = key.substr(7);
      const auto dot = rest.rfind('.');
      const std::string pass = rest.substr(0, dot);
      const std::string field = rest.substr(dot + 1);
      auto it = passMap.find(pass);
      if (it == passMap.end()) it = passMap.emplace(pass, Value::object()).first;
      it->second.set(field, std::move(leaf));
    } else {
      metrics.set(key, std::move(leaf));
    }
  };
  for (const auto& [key, value] : p50s) place(key, statsLeaf(value));
  for (const auto& [key, reason] : unavailable) place(key, unavailableLeaf(reason));
  for (auto& [name, value] : passMap) passes.set(name, std::move(value));
  metrics.set("zones", std::move(zones));
  metrics.set("passes", std::move(passes));
  doc.set("metrics", std::move(metrics));
  return doc;
}

}  // namespace gate_test

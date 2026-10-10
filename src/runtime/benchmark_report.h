#pragma once

// Spec 0059 / ADR-0117 D6-D7 (Plan 0059 M9): the benchmark's result document
// (schema atlantis.benchmark/1) and its comparison with an earlier one.
//
// Private to the atlantis_runtime executable's startup layer, like cli.h and
// editor_attachment.h: NOT under src/runtime/include/, NOT compiled into
// atlantis_runtime_host. GPU-independent: it turns recorded frames into JSON
// and compares two documents, so it is unit-tested without a device. Pure
// functions over values; not thread-affine.

#include <atlantis/connection/json.h>
#include <atlantis/profile_recorder.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::runtime::benchmark {

inline constexpr std::string_view kSchema = "atlantis.benchmark/1";

struct BuildInfo {
  std::string type;      // the CMake configuration: Debug, Release, ...
  std::string compiler;
  std::string gitRevision;
  bool gitDirty = false;
  bool profiling = false;
  bool tracy = false;
  bool gpuMarkers = false;
  bool validation = false;  // validation layers effectively on for this run
};

struct SystemInfo {
  std::string os;
  std::string cpu;
};

struct SceneInfo {
  std::string name;
  std::string guid;
  std::string contentPin;  // the pinned content's source commit; empty for built-in scenes
};

struct Settings {
  unsigned int width = 0;   // the extent the run actually rendered at
  unsigned int height = 0;
  unsigned int warmup = 0;
  unsigned int frames = 0;
  std::string variant;
  bool gpuTiming = true;
};

// One measured frame: the recorder's record with its late GPU results merged in,
// and the wall time to the next frame's start.
struct MeasuredFrame {
  atlantis::profile::FrameRecord record;
  double frameMs = 0.0;
};

struct ReportInput {
  BuildInfo build;
  SystemInfo system;
  atlantis::profile::DeviceInfo device;
  SceneInfo scene;
  Settings settings;
  std::size_t discardedFrames = 0;  // warmup frames, run and not measured
  std::size_t skippedFrames = 0;    // measured-range frames that never reached Present
  std::vector<MeasuredFrame> frames;  // presented frames only, in order
  bool includeSeries = false;
};

// The document. Members appear in the fixed order of the schema, so the same
// input always yields the same bytes. Metrics that the hardware or the build
// cannot supply are {"unavailable": "<reason>"}, never zero.
[[nodiscard]] atlantis::connection::json::Value buildDocument(const ReportInput& input);

// A document's `comparison` section against `baseline`: whether they are
// comparable (equal schema, scene guid and content pin, resolution, variant,
// GPU timing, build configuration and options, GPU and driver), why not if not,
// and for every metric present on both sides the change in avg, p50, p95 and p99.
// No threshold and no verdict.
[[nodiscard]] atlantis::connection::json::Value compareDocuments(const atlantis::connection::json::Value& baseline,
                                                                 const atlantis::connection::json::Value& current);

// A short human-readable summary of a document, for a run without --json.
[[nodiscard]] std::string summarize(const atlantis::connection::json::Value& document);

}  // namespace atlantis::runtime::benchmark

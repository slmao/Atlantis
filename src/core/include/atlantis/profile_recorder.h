#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Spec 0059 / ADR-0114 D4-D5: the in-process recorder behind the ATL_PROFILE_*
// macros (profile.h). It keeps, per frame, zone durations, counters and GPU
// region times in fixed-capacity storage, and a small ring of the last frames.
// It is independent of Tracy: the benchmark reads its numbers directly.
//
// This is global diagnostics state in Core -- the second instance of the
// exception AGENTS.md allows for logging infrastructure (after the log sink),
// not a precedent for engine state. It holds measurements only and never
// influences engine behavior.
//
// Thread-safety: NOT thread-safe. Frame thread only (ADR-0004); no locks.
//
// Always compiled (the statistics and the recorder are testable in every build
// configuration); it is only fed when ATLANTIS_PROFILING is on, and nothing
// reads it in a build without the profiling macros.
namespace atlantis::profile {

// A small integer naming a zone, counter or GPU region. Ids are dense from 0.
using NameId = std::uint32_t;
inline constexpr NameId kInvalidName = 0xFFFFFFFFu;

inline constexpr std::size_t kMaxNames = 256;
inline constexpr std::size_t kMaxGpuRegions = 128;
inline constexpr std::size_t kFrameRing = 4;

// Interns `name`, allocating only the first time a distinct string is seen.
// kInvalidName if the table (kMaxNames) is full; such events are counted by
// droppedNames() and never an error.
[[nodiscard]] NameId internName(std::string_view name);
[[nodiscard]] std::string_view nameOf(NameId id);
[[nodiscard]] std::size_t nameCount();
[[nodiscard]] std::size_t droppedNames();

// One GPU region, in nanoseconds on the GPU timeline (only differences are
// meaningful). `depth` is the region's nesting depth, 0 for a top-level pass.
struct GpuRegion {
  NameId name = kInvalidName;
  std::uint32_t depth = 0;
  std::uint64_t beginNs = 0;
  std::uint64_t endNs = 0;
};

// Everything recorded for one frame. Zone durations are inclusive; same-named
// zones in one frame are summed over their outermost occurrences.
struct FrameRecord {
  std::uint64_t frameId = 0;  // 1-based sequence; 0 means "no frame"
  std::uint64_t startNs = 0;
  bool closed = false;     // the next frame has begun
  bool presented = false;  // a "Present" zone ended in this frame
  bool gpuDelivered = false;
  std::array<std::uint64_t, kMaxNames> zoneNs{};
  std::array<std::uint32_t, kMaxNames> zoneCount{};
  std::array<double, kMaxNames> counter{};
  std::bitset<kMaxNames> counterSet;  // which counters were written this frame
  std::array<GpuRegion, kMaxGpuRegions> regions{};
  std::uint32_t regionCount = 0;
  std::uint32_t droppedRegions = 0;
};

// What the Vulkan Backend and the Windows Platform publish for the benchmark's
// metadata (Plan 0059 J2). Every field is optional: empty / 0 means unknown.
struct DeviceInfo {
  std::string gpu;
  std::string driver;
  std::string vulkanApi;
  double timestampPeriodNs = 0.0;
  bool timestampsSupported = false;
  std::string presentMode;
  double refreshHz = 0.0;
};

// Control. startRecording() clears the frame ring (frame ids stay monotonic);
// events are recorded only while recording.
void startRecording();
void stopRecording();
[[nodiscard]] bool isRecording() noexcept;

// Time source. Tests install their own; nullptr restores steady_clock.
using ClockFn = std::uint64_t (*)();
void setClockForTesting(ClockFn clock);
[[nodiscard]] std::uint64_t nowNs();

// Events. The ATL_PROFILE_* macros call these; tests may call them directly.
// beginFrame() closes the current frame and starts the next.
void beginFrame();
// Returns the start time. Nesting of the same id is tracked, so only the
// outermost occurrence is summed.
[[nodiscard]] std::uint64_t zoneBegin(NameId id);
void zoneEnd(NameId id, std::uint64_t startNs);
void counterValue(NameId id, double value);  // gauge: last write wins
void counterAdd(NameId id, double delta);    // accumulator: sums within a frame
// GPU region results, delivered one frame late for the frame that recorded
// them. Dropped (returns false) if that frame has left the ring.
bool deliverGpuRegions(std::uint64_t frameId, std::span<const GpuRegion> regions, std::uint32_t dropped);

// Readout.
[[nodiscard]] std::uint64_t currentFrameId() noexcept;
// Copies the record for `frameId` if it is still in the ring.
[[nodiscard]] bool readFrame(std::uint64_t frameId, FrameRecord& out);

// Metadata.
void setDeviceInfo(const DeviceInfo& info);
[[nodiscard]] DeviceInfo deviceInfo();

namespace detail {
// True while recording. Read inline by the macros' fast path; written only by
// startRecording()/stopRecording().
extern bool gRecording;
}  // namespace detail

}  // namespace atlantis::profile

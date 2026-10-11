#include <atlantis/profile_recorder.h>

#include <chrono>
#include <deque>
#include <unordered_map>

namespace atlantis::profile {

namespace detail {
bool gRecording = false;
}  // namespace detail

namespace {

// The interned-name table. std::deque keeps every string at a stable address,
// so the string_view keys of `index` and the views handed out stay valid.
struct NameTable {
  std::deque<std::string> names;
  std::unordered_map<std::string_view, NameId> index;
  std::size_t dropped = 0;
};

NameTable& nameTable() {
  static NameTable table;
  return table;
}

struct State {
  std::array<FrameRecord, kFrameRing> ring;
  std::uint64_t frameCounter = 0;  // monotonic across recordings
  FrameRecord* current = nullptr;
  std::array<std::uint32_t, kMaxNames> depth{};
  std::array<std::uint64_t, kMaxNames> outerStart{};
  ClockFn clock = nullptr;
  bool gpuTiming = true;
  DeviceInfo device;
};

State& state() {
  static State s;
  return s;
}

std::uint64_t steadyNowNs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

NameId presentName() {
  static const NameId id = internName("Present");
  return id;
}

}  // namespace

NameId internName(std::string_view name) {
  NameTable& table = nameTable();
  if (const auto it = table.index.find(name); it != table.index.end()) return it->second;
  if (table.names.size() >= kMaxNames) {
    ++table.dropped;
    return kInvalidName;
  }
  const NameId id = static_cast<NameId>(table.names.size());
  table.names.emplace_back(name);
  table.index.emplace(std::string_view(table.names.back()), id);
  return id;
}

std::string_view nameOf(NameId id) {
  const NameTable& table = nameTable();
  return id < table.names.size() ? std::string_view(table.names[id]) : std::string_view{};
}

std::size_t nameCount() { return nameTable().names.size(); }
std::size_t droppedNames() { return nameTable().dropped; }

void startRecording() {
  State& s = state();
  for (FrameRecord& record : s.ring) record = FrameRecord{};
  s.current = nullptr;
  s.depth.fill(0);
  s.outerStart.fill(0);
  detail::gRecording = true;
}

void stopRecording() { detail::gRecording = false; }

bool isRecording() noexcept { return detail::gRecording; }

void setClockForTesting(ClockFn clock) { state().clock = clock; }

std::uint64_t nowNs() {
  const State& s = state();
  return s.clock != nullptr ? s.clock() : steadyNowNs();
}

void beginFrame() {
  if (!detail::gRecording) return;
  State& s = state();
  if (s.current != nullptr) s.current->closed = true;
  ++s.frameCounter;
  FrameRecord& record = s.ring[s.frameCounter % kFrameRing];
  record = FrameRecord{};
  record.frameId = s.frameCounter;
  record.startNs = nowNs();
  s.current = &record;
}

std::uint64_t zoneBegin(NameId id) {
  const std::uint64_t start = nowNs();
  if (id >= kMaxNames) return start;
  State& s = state();
  if (s.depth[id]++ == 0) s.outerStart[id] = start;
  return start;
}

void zoneEnd(NameId id, std::uint64_t /*startNs*/) {
  if (id >= kMaxNames) return;
  State& s = state();
  if (s.depth[id] == 0) return;  // began in an earlier recording
  if (--s.depth[id] != 0) return;
  if (s.current == nullptr) return;
  const std::uint64_t end = nowNs();
  const std::uint64_t begin = s.outerStart[id];
  s.current->zoneNs[id] += end >= begin ? end - begin : 0;
  ++s.current->zoneCount[id];
  if (id == presentName()) s.current->presented = true;
}

void counterValue(NameId id, double value) {
  State& s = state();
  if (id >= kMaxNames || s.current == nullptr) return;
  s.current->counter[id] = value;
  s.current->counterSet.set(id);
}

void counterAdd(NameId id, double delta) {
  State& s = state();
  if (id >= kMaxNames || s.current == nullptr) return;
  s.current->counter[id] += delta;
  s.current->counterSet.set(id);
}

bool deliverGpuRegions(std::uint64_t frameId, std::span<const GpuRegion> regions, std::uint32_t dropped) {
  State& s = state();
  if (frameId == 0) return false;
  FrameRecord& record = s.ring[frameId % kFrameRing];
  if (record.frameId != frameId) return false;
  const std::size_t count = regions.size() < kMaxGpuRegions ? regions.size() : kMaxGpuRegions;
  for (std::size_t i = 0; i < count; ++i) record.regions[i] = regions[i];
  record.regionCount = static_cast<std::uint32_t>(count);
  record.droppedRegions = dropped + static_cast<std::uint32_t>(regions.size() - count);
  record.gpuDelivered = true;
  return true;
}

bool counterValueForFrame(std::uint64_t frameId, NameId id, double value) {
  State& s = state();
  if (frameId == 0 || id >= kMaxNames) return false;
  FrameRecord& record = s.ring[frameId % kFrameRing];
  if (record.frameId != frameId) return false;
  record.counter[id] = value;
  record.counterSet.set(id);
  return true;
}

void setGpuTimingEnabled(bool enabled) noexcept { state().gpuTiming = enabled; }
bool gpuTimingEnabled() noexcept { return state().gpuTiming; }

std::uint64_t currentFrameId() noexcept { return state().current != nullptr ? state().current->frameId : 0; }

bool readFrame(std::uint64_t frameId, FrameRecord& out) {
  const State& s = state();
  if (frameId == 0) return false;
  const FrameRecord& record = s.ring[frameId % kFrameRing];
  if (record.frameId != frameId) return false;
  out = record;
  return true;
}

void setDeviceInfo(const DeviceInfo& info) { state().device = info; }
DeviceInfo deviceInfo() { return state().device; }

}  // namespace atlantis::profile

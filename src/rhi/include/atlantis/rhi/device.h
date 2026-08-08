#pragma once

namespace atlantis::rhi {

// Represents a logical GPU device and its queues. Has no window/surface
// knowledge. Owned by whoever constructs it (a verification demo today;
// Runtime, once that module exists). Not internally thread-safe;
// construction/destruction happen on the single Phase 1 frame thread
// (ADR-0004). Intentionally declares no method beyond the destructor in
// this spec's scope -- nothing here submits GPU work or queries a queue
// directly; a future RenderGraph/CommandList spec extends this interface,
// not sketched here.
class Device {
 public:
  virtual ~Device() = default;
};

}  // namespace atlantis::rhi

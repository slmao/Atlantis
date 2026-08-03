#pragma once

#include <chrono>

namespace atlantis::platform {

// Monotonic elapsed-time facility — see specs/0002-platform-foundation.md
// Application Timing. Portable; not gated per-OS (see
// plans/0002-platform-foundation.md Section 5 for why this lives in
// Platform rather than Atlantis Core).
using TimePoint = std::chrono::steady_clock::time_point;

[[nodiscard]] TimePoint monotonicNow();

}  // namespace atlantis::platform

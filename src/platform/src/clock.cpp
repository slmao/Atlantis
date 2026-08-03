#include <atlantis/platform/clock.h>

namespace atlantis::platform {

TimePoint monotonicNow() {
  return std::chrono::steady_clock::now();
}

}  // namespace atlantis::platform

#include <atlantis/runtime/platform_session.h>

#include <utility>

namespace atlantis::runtime {

PlatformSession::~PlatformSession() {
  if (active_) {
    platform::shutdown();
    for (const auto& event : platform::processEvents()) {
      static_cast<void>(event);  // final drain -- logged elsewhere by the caller, if at all
    }
    active_ = false;
  }
}

PlatformSession::PlatformSession(PlatformSession&& other) noexcept : active_(other.active_) {
  other.active_ = false;
}

atlantis::Result<PlatformSession, atlantis::platform::PlatformError> createPlatformSession() {
  auto initResult = platform::initialize();
  if (initResult.isErr()) {
    return atlantis::Result<PlatformSession, atlantis::platform::PlatformError>::Err(initResult.error());
  }
  PlatformSession session;
  session.active_ = true;
  return atlantis::Result<PlatformSession, atlantis::platform::PlatformError>::Ok(std::move(session));
}

}  // namespace atlantis::runtime

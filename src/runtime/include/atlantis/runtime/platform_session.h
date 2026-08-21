#pragma once

#include <atlantis/platform/platform.h>
#include <atlantis/result.h>

namespace atlantis::runtime {

// See Plan 0013 Section D4a. An RAII guard around platform::initialize()/
// platform::shutdown() -- the single, structural fix for the window-vs.-
// GPU-resource destruction order: declared as RuntimeApplication's FIRST
// member so it is destroyed LAST, by ordinary C++ reverse-declaration-
// order destruction. platform::shutdown() has exactly one call site in
// the whole Runtime module: this type's own destructor. Not internally
// thread-safe; caller-thread-only (ADR-0004), matching every other type
// in this module.
class PlatformSession {
 public:
  PlatformSession() noexcept = default;
  ~PlatformSession();

  PlatformSession(const PlatformSession&) = delete;
  PlatformSession& operator=(const PlatformSession&) = delete;

  PlatformSession(PlatformSession&& other) noexcept;
  PlatformSession& operator=(PlatformSession&& other) noexcept;

  [[nodiscard]] bool isActive() const noexcept { return active_; }

 private:
  friend atlantis::Result<PlatformSession, atlantis::platform::PlatformError> createPlatformSession();

  bool active_ = false;
};

// Calls platform::initialize(). On Ok, returns a PlatformSession with
// isActive() true. On Err, returns Err directly -- no PlatformSession is
// ever constructed with isActive() true unless initialize() genuinely
// succeeded, so its destructor never calls platform::shutdown() for a
// session that was never really established.
[[nodiscard]] atlantis::Result<PlatformSession, atlantis::platform::PlatformError> createPlatformSession();

}  // namespace atlantis::runtime

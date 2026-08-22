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
//
// Move-assignment is deleted, not just move-construction kept (PR #63
// review round): an assignment operator would have to call
// platform::shutdown() itself whenever the assignment target already held
// an active session, giving platform::shutdown() a second call site and
// letting a still-active session be torn down without any of the ordering
// guarantee RuntimeApplication's member-declaration order otherwise
// provides. Wiring an active session into a RuntimeApplication is done
// exclusively via move CONSTRUCTION (see runtime_application.h's private
// constructor) -- never by assigning into a default-constructed, inactive
// instance.
class PlatformSession {
 public:
  PlatformSession() noexcept = default;
  ~PlatformSession();

  PlatformSession(const PlatformSession&) = delete;
  PlatformSession& operator=(const PlatformSession&) = delete;

  PlatformSession(PlatformSession&& other) noexcept;
  PlatformSession& operator=(PlatformSession&&) = delete;

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

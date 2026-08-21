#include <atlantis/runtime/lifecycle_state.h>

#include <atlantis/assert.h>

namespace atlantis::runtime {

void RuntimeLifecycleTracker::beginInitializing() {
  ATLANTIS_CHECK_MSG(state_ == RuntimeLifecycleState::Uninitialized,
                      "beginInitializing() requires Uninitialized");
  state_ = RuntimeLifecycleState::Initializing;
}

void RuntimeLifecycleTracker::markRunning() {
  ATLANTIS_CHECK_MSG(state_ == RuntimeLifecycleState::Initializing, "markRunning() requires Initializing");
  state_ = RuntimeLifecycleState::Running;
  everRan_ = true;
}

void RuntimeLifecycleTracker::markFailed() {
  ATLANTIS_CHECK_MSG(state_ == RuntimeLifecycleState::Initializing || state_ == RuntimeLifecycleState::Running,
                      "markFailed() requires Initializing or Running");
  state_ = RuntimeLifecycleState::Failed;
}

void RuntimeLifecycleTracker::beginShutdown() {
  if (state_ == RuntimeLifecycleState::ShuttingDown || state_ == RuntimeLifecycleState::ShutDown) {
    return;  // idempotent
  }
  ATLANTIS_CHECK_MSG(state_ == RuntimeLifecycleState::Initializing || state_ == RuntimeLifecycleState::Running ||
                          state_ == RuntimeLifecycleState::Failed,
                      "beginShutdown() requires Initializing, Running, or Failed");
  state_ = RuntimeLifecycleState::ShuttingDown;
}

void RuntimeLifecycleTracker::markShutDown() {
  ATLANTIS_CHECK_MSG(state_ == RuntimeLifecycleState::ShuttingDown, "markShutDown() requires ShuttingDown");
  state_ = RuntimeLifecycleState::ShutDown;
}

}  // namespace atlantis::runtime

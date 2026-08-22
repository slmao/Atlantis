#pragma once

namespace atlantis::runtime {

// See Plan 0013 Section D2. GPU-independent: this type names no
// Platform, RHI, or Vulkan Backend type, so it is directly unit-testable
// without a device or a window. Not thread-safe; caller-thread-only
// (ADR-0004), matching every other Phase 1 type in this codebase.
enum class RuntimeLifecycleState {
  Uninitialized,
  Initializing,
  Running,
  ShuttingDown,
  ShutDown,
  Failed,
};

// Tracks RuntimeApplication's own lifecycle transitions and enforces the
// legal transition table below. Every transition not explicitly legal is
// a programmer-error precondition violation (ATLANTIS_CHECK_MSG), per
// AGENTS.md's existing convention -- not a new assertion mechanism.
class RuntimeLifecycleTracker {
 public:
  RuntimeLifecycleTracker() noexcept = default;

  [[nodiscard]] RuntimeLifecycleState state() const noexcept { return state_; }

  // True once markRunning() has ever been called, even after a later
  // transition to Failed/ShuttingDown/ShutDown -- never reset. This is
  // what distinguishes "failed during the six fixed initialization
  // steps, before any frame ever ran" from "failed during the frame
  // loop, after Running was genuinely reached" -- the exact distinction
  // RuntimeApplication::shutdown()'s waitIdle() usage depends on (a
  // Device that was never handed a command list has nothing to wait
  // for; one that reached Running may have submitted real GPU work).
  [[nodiscard]] bool hasEverRun() const noexcept { return everRan_; }

  // Uninitialized -> Initializing.
  void beginInitializing();

  // Initializing -> Running. Sets hasEverRun() true, permanently.
  void markRunning();

  // Initializing | Running -> Failed.
  void markFailed();

  // Initializing | Running | Failed -> ShuttingDown.
  // ShuttingDown | ShutDown -> no-op (idempotent).
  void beginShutdown();

  // ShuttingDown -> ShutDown.
  void markShutDown();

 private:
  RuntimeLifecycleState state_ = RuntimeLifecycleState::Uninitialized;
  bool everRan_ = false;
};

}  // namespace atlantis::runtime

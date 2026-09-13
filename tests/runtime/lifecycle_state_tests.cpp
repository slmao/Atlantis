#include <atlantis/assert.h>
#include <atlantis/runtime/lifecycle_state.h>

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using atlantis::runtime::RuntimeLifecycleState;
using atlantis::runtime::RuntimeLifecycleTracker;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// See tests/render_graph/handle_ownership_tests.cpp's own identical
// pattern (tests/core/assert_tests.cpp establishes it) -- installs a
// recording, non-terminating replacement failure handler for the
// lifetime of one test, restoring whatever was previously installed on
// destruction.
class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<RecordedFailure>& recorded)
      : previous_(atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
          recorded.push_back({std::string(info.expression), std::string(info.message)});
        })) {}

  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

}  // namespace

TEST_CASE("A freshly-constructed tracker starts Uninitialized with hasEverRun() false", "[runtime][lifecycle_state]") {
  const RuntimeLifecycleTracker tracker;
  REQUIRE(tracker.state() == RuntimeLifecycleState::Uninitialized);
  REQUIRE_FALSE(tracker.hasEverRun());
}

TEST_CASE("The full legal happy-path sequence succeeds with no assertion failure", "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  REQUIRE(tracker.state() == RuntimeLifecycleState::Initializing);

  tracker.markRunning();
  REQUIRE(tracker.state() == RuntimeLifecycleState::Running);
  REQUIRE(tracker.hasEverRun());

  tracker.beginShutdown();
  REQUIRE(tracker.state() == RuntimeLifecycleState::ShuttingDown);

  tracker.markShutDown();
  REQUIRE(tracker.state() == RuntimeLifecycleState::ShutDown);

  REQUIRE(recorded.empty());
}

TEST_CASE("beginShutdown() is legal directly from Initializing (an early-init-failure path)",
          "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.markFailed();
  REQUIRE(tracker.state() == RuntimeLifecycleState::Failed);
  REQUIRE_FALSE(tracker.hasEverRun());  // never reached Running

  tracker.beginShutdown();
  tracker.markShutDown();
  REQUIRE(tracker.state() == RuntimeLifecycleState::ShutDown);
  REQUIRE(recorded.empty());
}

TEST_CASE("hasEverRun() stays true after Running transitions to Failed and then ShutDown",
          "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.markRunning();
  tracker.markFailed();
  REQUIRE(tracker.hasEverRun());

  tracker.beginShutdown();
  tracker.markShutDown();
  REQUIRE(tracker.hasEverRun());
  REQUIRE(recorded.empty());
}

TEST_CASE("beginShutdown() is idempotent from ShuttingDown and from ShutDown", "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.markRunning();

  tracker.beginShutdown();
  tracker.beginShutdown();  // idempotent no-op, still ShuttingDown
  REQUIRE(tracker.state() == RuntimeLifecycleState::ShuttingDown);

  tracker.markShutDown();
  tracker.beginShutdown();  // idempotent no-op, still ShutDown
  REQUIRE(tracker.state() == RuntimeLifecycleState::ShutDown);

  REQUIRE(recorded.empty());
}

TEST_CASE("markRunning() before beginInitializing() triggers the assertion policy", "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.markRunning();

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("markFailed() from Uninitialized triggers the assertion policy", "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.markFailed();

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("markShutDown() from Running (skipping beginShutdown()) triggers the assertion policy",
          "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.markRunning();
  tracker.markShutDown();

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("Running survives a Created-Destroyed-Created-shaped event sequence with no Failed transition",
          "[runtime][lifecycle_state]") {
  // Plan 0034 Milestone 6 (human-directed, disclosed deviation --
  // runtime_host internals, per Plan 0034's own not-touched list): the
  // state-machine-level invariant runtime_application.cpp's own
  // SurfaceCreated/SurfaceDestroyed handling in runFrame() now relies
  // on. That fix's own logic is not itself GPU-independently testable
  // -- it calls real atlantis::vulkan_backend::createPresentation() and
  // Device::waitIdle(), both genuine GPU calls this test suite
  // deliberately does not make (this file, unlike
  // tests/runtime/runtime_smoke_gpu_tests.cpp, is GPU-independent by
  // design). This test instead confirms, at the pure
  // RuntimeLifecycleTracker level, exactly the invariant the fix
  // depends on: Running is a legal state to remain in indefinitely
  // across a sequence standing in for Android's own repeated surface
  // destroy/recreate cycle (ADR-0013) -- no hidden requirement exists
  // to transition through Failed, or any other state, merely because
  // such a cycle occurred while Running. RuntimeLifecycleTracker itself
  // has no SurfaceCreated/SurfaceDestroyed-specific method (those are
  // Presentation-level concerns the tracker never represents), so this
  // is the most this state machine's own GPU-independent test
  // framework can directly assert -- the actual event-dispatch and
  // Presentation teardown/rebuild logic remains covered only by manual
  // on-device verification, disclosed as such.
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.markRunning();
  REQUIRE(tracker.state() == RuntimeLifecycleState::Running);
  REQUIRE(tracker.hasEverRun());

  for (int cycle = 0; cycle < 3; ++cycle) {
    // Created -> ... -> Destroyed -> ... -> Created again: the tracker
    // itself is never touched by either event (runFrame()'s own fix
    // deliberately makes no lifecycle_ call for SurfaceDestroyed at
    // all) -- state and hasEverRun() must both stay exactly as they
    // were, every single cycle, with no assertion firing.
    REQUIRE(tracker.state() == RuntimeLifecycleState::Running);
    REQUIRE(tracker.hasEverRun());
  }

  tracker.beginShutdown();
  tracker.markShutDown();
  REQUIRE(tracker.state() == RuntimeLifecycleState::ShutDown);
  REQUIRE(tracker.hasEverRun());
  REQUIRE(recorded.empty());
}

TEST_CASE("beginInitializing() called twice triggers the assertion policy", "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.beginInitializing();

  REQUIRE(recorded.size() == 1);
}

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

TEST_CASE("beginInitializing() called twice triggers the assertion policy", "[runtime][lifecycle_state]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RuntimeLifecycleTracker tracker;
  tracker.beginInitializing();
  tracker.beginInitializing();

  REQUIRE(recorded.size() == 1);
}

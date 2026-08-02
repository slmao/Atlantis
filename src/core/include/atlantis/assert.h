#pragma once

#include <functional>
#include <source_location>
#include <string_view>

namespace atlantis {

// See ADR-0009. No exception is thrown by either macro below; failures
// are reported through a replaceable handler instead.
struct AssertFailureInfo {
  std::string_view expression;
  std::string_view message;  // empty for the no-message macro variants
  std::source_location location;
};

using AssertFailureHandler = std::function<void(const AssertFailureInfo&)>;

namespace assertions {

// Installs a replacement failure handler and returns the previous one,
// so callers (tests) can restore it. Passing an empty std::function
// restores the default "log via ATLANTIS_LOG_FATAL, then std::abort()"
// handler. Thread-safe.
AssertFailureHandler setFailureHandler(AssertFailureHandler handler);

// Invoked by ATLANTIS_ASSERT/ATLANTIS_CHECK on failure. Not intended to
// be called directly from ordinary code.
void reportFailure(const AssertFailureInfo& info);

}  // namespace assertions

}  // namespace atlantis

// Always evaluated, Debug and Release. For invariants that must remain
// validated in production. See ADR-0009.
#define ATLANTIS_CHECK(condition)                                          \
  do {                                                                     \
    if (!(condition)) {                                                   \
      ::atlantis::assertions::reportFailure(::atlantis::AssertFailureInfo{ \
          #condition, {}, std::source_location::current()});               \
    }                                                                      \
  } while (false)

#define ATLANTIS_CHECK_MSG(condition, msg)                                 \
  do {                                                                     \
    if (!(condition)) {                                                   \
      ::atlantis::assertions::reportFailure(::atlantis::AssertFailureInfo{ \
          #condition, (msg), std::source_location::current()});            \
    }                                                                      \
  } while (false)

// Debug/development invariant checking only. Compiled to nothing in
// Release builds (NDEBUG defined) — the condition is not evaluated, so
// assert conditions must be free of required side effects. See ADR-0009.
#if defined(NDEBUG)
#define ATLANTIS_ASSERT(condition) ((void)0)
#define ATLANTIS_ASSERT_MSG(condition, msg) ((void)0)
#else
#define ATLANTIS_ASSERT(condition) ATLANTIS_CHECK(condition)
#define ATLANTIS_ASSERT_MSG(condition, msg) ATLANTIS_CHECK_MSG(condition, msg)
#endif

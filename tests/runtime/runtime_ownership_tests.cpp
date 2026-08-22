#include <atlantis/runtime/platform_session.h>
#include <atlantis/runtime/runtime_application.h>

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

// PR #63 review round (Plan 0013 Section D4a amendment): PlatformSession
// and RuntimeApplication must be move-constructible but NOT move-
// assignable. A move-assignment operator would have to call
// platform::shutdown() itself whenever the assignment target already held
// an active session -- giving platform::shutdown() a second call site
// beyond PlatformSession's own destructor, and (for RuntimeApplication,
// whose member-wise move-assignment would run in member DECLARATION
// order) closing the window before the GPU resources declared below
// platformSession_ are torn down. These static_asserts lock the deletion
// in at compile time so it cannot silently regress back to `= default`.

namespace atlantis::runtime {

static_assert(std::is_move_constructible_v<PlatformSession>);
static_assert(!std::is_move_assignable_v<PlatformSession>);
static_assert(!std::is_copy_constructible_v<PlatformSession>);
static_assert(!std::is_copy_assignable_v<PlatformSession>);

static_assert(std::is_move_constructible_v<RuntimeApplication>);
static_assert(!std::is_move_assignable_v<RuntimeApplication>);
static_assert(!std::is_copy_constructible_v<RuntimeApplication>);
static_assert(!std::is_copy_assignable_v<RuntimeApplication>);

}  // namespace atlantis::runtime

TEST_CASE("A default-constructed PlatformSession is inactive", "[runtime][ownership]") {
  // No platform::initialize() call anywhere in this test -- PlatformSession's
  // default constructor only sets active_ = false, so this stays
  // GPU/window-independent, matching this suite's own boundary.
  const atlantis::runtime::PlatformSession session;
  REQUIRE_FALSE(session.isActive());
}

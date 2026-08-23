#include <atlantis/asset_system/validated_scene_data.h>

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using atlantis::asset_system::ValidatedSceneData;

// V11: ValidatedSceneData has no public default constructor of any
// kind (ADR-0053's own Human Review Correction, 2026-08-23) -- a
// type-level guarantee, not a documented convention. This is the
// compile-time half of V11; the runtime half (no external code can
// name the private non-default constructor, mutate a field, or obtain
// a mutable reference) is demonstrated below as documented,
// intentionally-uncompilable examples -- matching EntityId's own V27
// precedent -- rather than built, since a real negative-compilation
// test harness is out of this repository's own established scope.
static_assert(!std::is_default_constructible_v<ValidatedSceneData>);
static_assert(std::is_copy_constructible_v<ValidatedSceneData>);
static_assert(std::is_move_constructible_v<ValidatedSceneData>);
static_assert(std::is_copy_assignable_v<ValidatedSceneData>);
static_assert(std::is_move_assignable_v<ValidatedSceneData>);

// V11: every accessor returns a plain value or a const reference --
// never a mutable one -- confirmed at compile time, matching
// runtime_ownership_tests.cpp's own established static_assert pattern.
static_assert(std::is_same_v<decltype(std::declval<const ValidatedSceneData&>().nodeCount()), std::size_t>);
static_assert(!std::is_reference_v<decltype(std::declval<const ValidatedSceneData&>().nodeCount())>);
static_assert(std::is_const_v<
               std::remove_reference_t<decltype(std::declval<const ValidatedSceneData&>().node(0))>>);

// Uncompilable-if-uncommented documented examples, matching EntityId's
// own V27 precedent exactly -- this file does not, and must not,
// contain a build target that actually compiles any of these:
//
//   ValidatedSceneData empty;                       // no default constructor
//   ValidatedSceneData bogus({}, {}, std::nullopt);  // the three-argument
//                                                     // constructor is private
//   someValidatedSceneData.node(0) = {};             // node() returns const&
//
// Copy/move preserving an already-decoded instance's own accessor
// values is verified against a real decodeScene() result once that
// function exists (Plan 0015 Step 4, decode_scene_tests.cpp) -- there
// is no other way to obtain a ValidatedSceneData instance at all, by
// this same design, so no such test can exist before then.

TEST_CASE("ValidatedSceneData's own default-constructibility is rejected at compile time", "[asset_system][scene]") {
  // The static_asserts above already prove this at compile time; this
  // TEST_CASE exists so ctest reports at least one runtime case for
  // this file rather than a build-only, invisible-to-ctest check.
  SUCCEED("static_assert(!std::is_default_constructible_v<ValidatedSceneData>) above already verified this");
}

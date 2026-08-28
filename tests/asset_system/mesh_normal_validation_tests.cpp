#include <atlantis/asset_system/mesh_source.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Plan 0020 Section P8: three distinct, independently-implementable
// kinds of boundary testing for the normal numeric contract (Spec 0020
// D3) -- this file covers Kind 1 (pure comparator, exact boundary, via
// std::nextafter on double literals, never through float components)
// and Kind 2 (real float components, unambiguous margin, no claim about
// landing on the exact boundary). Kind 3 (full parse/decode-path
// integration) lives in mesh_source_tests.cpp/mesh_artifact_tests.cpp,
// exercising the real, shared atlantis::asset_system::detail functions
// through parseMeshSource()/decodeMeshArtifact() end to end, not
// duplicated here.

using namespace atlantis::asset_system;

TEST_CASE("isNormalLengthSquaredInTolerance: Kind 1, pure comparator, exact inclusive boundaries",
          "[asset_system]") {
  CHECK(detail::isNormalLengthSquaredInTolerance(0.9801));
  CHECK(detail::isNormalLengthSquaredInTolerance(1.0201));
}

TEST_CASE("isNormalLengthSquaredInTolerance: Kind 1, pure comparator, smallest representable step outside each "
          "bound is rejected",
          "[asset_system]") {
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(std::nextafter(0.9801, 0.0)));
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(std::nextafter(1.0201, 2.0)));
}

TEST_CASE("isNormalLengthSquaredInTolerance: Kind 1, pure comparator, comfortably inside and outside",
          "[asset_system]") {
  CHECK(detail::isNormalLengthSquaredInTolerance(1.0));
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(0.0));
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(3.0));
}

TEST_CASE("computeNormalLengthSquared/isNormalLengthSquaredInTolerance: Kind 2, real float components, clearly "
          "inside",
          "[asset_system]") {
  // Spec 0020 D5's own already-approved literal -- lengthSquared ~=
  // 0.999999964, comfortably inside [0.9801, 1.0201]. This layer makes
  // no claim about landing on the exact boundary; that is Kind 1's own
  // job alone.
  const double lengthSquared = detail::computeNormalLengthSquared(0.577350269f, 0.577350269f, 0.577350269f);
  CHECK(detail::isNormalLengthSquaredInTolerance(lengthSquared));
}

TEST_CASE("computeNormalLengthSquared/isNormalLengthSquaredInTolerance: Kind 2, real float components, clearly "
          "outside (grossly unnormalized)",
          "[asset_system]") {
  const double lengthSquared = detail::computeNormalLengthSquared(1.0f, 1.0f, 1.0f);
  CHECK(lengthSquared == 3.0);
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(lengthSquared));
}

TEST_CASE("computeNormalLengthSquared/isNormalLengthSquaredInTolerance: Kind 2, exact zero vector",
          "[asset_system]") {
  const double lengthSquared = detail::computeNormalLengthSquared(0.0f, 0.0f, 0.0f);
  CHECK(lengthSquared == 0.0);
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(lengthSquared));
}

TEST_CASE("computeNormalLengthSquared/isNormalLengthSquaredInTolerance: Kind 2, a -0.0f component behaves "
          "identically to +0.0f",
          "[asset_system]") {
  // Spec 0020 D3: std::isfinite(-0.0f) is true (accepted by the
  // finiteness check, exercised separately at Kind 3), and
  // (-0.0)^2 == +0.0 exactly under IEEE 754 -- confirmed here directly,
  // not left as an unverified assumption.
  const double withNegativeZero = detail::computeNormalLengthSquared(-0.0f, 0.816496581f, 0.577350269f);
  const double withPositiveZero = detail::computeNormalLengthSquared(0.0f, 0.816496581f, 0.577350269f);
  CHECK(withNegativeZero == withPositiveZero);
  CHECK(detail::isNormalLengthSquaredInTolerance(withNegativeZero));
}

TEST_CASE("computeNormalLengthSquared/isNormalLengthSquaredInTolerance: Kind 2, an extremely small non-zero vector "
          "is rejected, no separate near-zero special case",
          "[asset_system]") {
  const double lengthSquared = detail::computeNormalLengthSquared(1e-20f, 1e-20f, 1e-20f);
  CHECK_FALSE(detail::isNormalLengthSquaredInTolerance(lengthSquared));
}

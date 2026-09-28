#include <atlantis/asset_system/cook_environment.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <limits>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>
#include <string_view>

namespace {

// Plan 0047 M3: a deterministic, non-nil test identity per logical path, so
// a test's cross-references (scene -> mesh, material -> texture) agree.
[[nodiscard]] atlantis::asset_system::AssetGuid testAssetGuid(std::string_view key) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), key);
}

}  // namespace
using namespace atlantis::asset_system;

namespace {

[[nodiscard]] std::vector<float> validSource() { return std::vector<float>(4 * 2 * 4, 1.0F); }

}  // namespace

TEST_CASE("cookEnvironment rejects invalid logical paths before processing", "[asset_system]") {
  const auto source = validSource();
  const auto result = cookEnvironment(source.data(), 4, 2, "../outside.hdr", testAssetGuid("../outside.hdr"), {}, {});
  REQUIRE(result.isErr());
  CHECK(result.error() == EnvironmentCookError::LogicalPathInvalid);
}

TEST_CASE("cookEnvironment rejects non-equirectangular dimensions", "[asset_system]") {
  const auto source = validSource();
  CHECK(cookEnvironment(source.data(), 0, 0, "environment.hdr", testAssetGuid("environment.hdr"), {}, {}).error() ==
        EnvironmentCookError::InvalidSourceDimensions);
  CHECK(cookEnvironment(source.data(), 4, 4, "environment.hdr", testAssetGuid("environment.hdr"), {}, {}).error() ==
        EnvironmentCookError::InvalidSourceDimensions);
}

TEST_CASE("cookEnvironment rejects non-finite and negative RGB source values", "[asset_system]") {
  SECTION("NaN") {
    auto source = validSource();
    source[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(cookEnvironment(source.data(), 4, 2, "environment.hdr", testAssetGuid("environment.hdr"), {}, {}).error() ==
          EnvironmentCookError::NonFiniteSourceValue);
  }
  SECTION("infinity") {
    auto source = validSource();
    source[1] = std::numeric_limits<float>::infinity();
    CHECK(cookEnvironment(source.data(), 4, 2, "environment.hdr", testAssetGuid("environment.hdr"), {}, {}).error() ==
          EnvironmentCookError::NonFiniteSourceValue);
  }
  SECTION("negative") {
    auto source = validSource();
    source[2] = -0.01F;
    CHECK(cookEnvironment(source.data(), 4, 2, "environment.hdr", testAssetGuid("environment.hdr"), {}, {}).error() ==
          EnvironmentCookError::NegativeSourceValue);
  }
}

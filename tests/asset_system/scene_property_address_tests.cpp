#include <atlantis/asset_system/scene_property_address.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include <atlantis/asset_system/asset_guid.h>

// Plan 0049 M4 / P8 (Spec 0049 R7; rulings Q4, J8, J10).

namespace {

namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;
using atlantis::schema::fieldId;
using atlantis::schema::typeId;

[[nodiscard]] as::EntityGuid node() {
  return as::parseEntityGuid("0f1e2d3c-4b5a-4968-8778-a69584736251").value();
}

// J8: a nested field is addressed under its node component by its leaf id.
[[nodiscard]] scene::PropertyAddress fogDensity() {
  return {node(), typeId("asset_system::scene::Camera"), fieldId("asset_system::scene::CameraFog", "density")};
}

}  // namespace

TEST_CASE("property address: canonical text round-trips", "[asset_system][scene][address]") {
  const scene::PropertyAddress address = fogDensity();
  const std::string text = scene::toString(address);
  CHECK(text.size() == scene::kPropertyAddressTextLength);
  CHECK(text.starts_with("0f1e2d3c-4b5a-4968-8778-a69584736251/"));
  CHECK(text[36] == '/');
  CHECK(text[53] == '/');
  const auto parsed = scene::parsePropertyAddress(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == address);
}

TEST_CASE("property address: ids print as 16 lowercase hex digits", "[asset_system][scene][address]") {
  const scene::PropertyAddress address{node(), atlantis::schema::TypeId{0x00ab}, atlantis::schema::FieldId{0xf}};
  CHECK(scene::toString(address) == "0f1e2d3c-4b5a-4968-8778-a69584736251/00000000000000ab/000000000000000f");
}

TEST_CASE("property address: equality and ordering compare all three parts", "[asset_system][scene][address]") {
  const scene::PropertyAddress a = fogDensity();
  scene::PropertyAddress b = a;
  CHECK(a == b);
  b.field = fieldId("asset_system::scene::CameraFog", "height");
  CHECK(a != b);
  CHECK(((a < b) || (b < a)));
}

TEST_CASE("property address: parse rejections", "[asset_system][scene][address]") {
  const std::string valid = scene::toString(fogDensity());
  std::string text = valid;
  scene::PropertyAddressParseError expected{};

  SECTION("wrong length") {
    text += "0";
    expected = scene::PropertyAddressParseError::WrongLength;
  }
  SECTION("missing first separator") {
    text[36] = ':';
    expected = scene::PropertyAddressParseError::MissingSeparator;
  }
  SECTION("missing second separator") {
    text[53] = ':';
    expected = scene::PropertyAddressParseError::MissingSeparator;
  }
  SECTION("malformed GUID") {
    text[0] = 'G';
    expected = scene::PropertyAddressParseError::MalformedGuid;
  }
  SECTION("nil GUID") {
    text.replace(0, 36, "00000000-0000-0000-0000-000000000000");
    expected = scene::PropertyAddressParseError::NilGuid;
  }
  SECTION("uppercase hex id") {
    text[37] = 'A';
    expected = scene::PropertyAddressParseError::MalformedId;
  }
  SECTION("zero component id") {
    text.replace(37, 16, std::string(16, '0'));
    expected = scene::PropertyAddressParseError::ZeroId;
  }
  SECTION("zero field id") {
    text.replace(54, 16, std::string(16, '0'));
    expected = scene::PropertyAddressParseError::ZeroId;
  }

  const auto parsed = scene::parsePropertyAddress(text);
  REQUIRE(parsed.isErr());
  CHECK(parsed.error() == expected);
  CHECK(scene::toString(expected) != "Unknown");
}

#include <atlantis/asset_system/entity_ref.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace atlantis::asset_system;

namespace {

constexpr const char* kSceneText = "0047aaaa-0000-4000-8000-000000000001";
constexpr const char* kEntityText = "e68122c6-1bb2-8f1f-b185-358f58780b05";

[[nodiscard]] EntityRef sampleRef() {
  return EntityRef{parseAssetGuid(kSceneText).value(), parseEntityGuid(kEntityText).value()};
}

void requireParseError(const std::string& text, EntityRefParseError expected) {
  const auto result = parseEntityRef(text);
  INFO(text);
  REQUIRE(result.isErr());
  CHECK(toString(result.error()) == toString(expected));
}

}  // namespace

TEST_CASE("An EntityRef's text is <scene guid>/<entity guid>, 73 characters, and round-trips",
          "[asset_system][entity_ref]") {
  const EntityRef ref = sampleRef();
  const std::string text = toString(ref);
  CHECK(text == std::string(kSceneText) + "/" + kEntityText);
  CHECK(text.size() == kEntityRefTextLength);
  CHECK(kEntityRefTextLength == 73);

  const auto parsed = parseEntityRef(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == ref);
}

TEST_CASE("An EntityRef's binary form is 32 bytes, scene then entity, and round-trips",
          "[asset_system][entity_ref]") {
  const EntityRef ref = sampleRef();
  const EntityRefBytes bytes = entityRefToBytes(ref);
  static_assert(sizeof(EntityRefBytes) == 32);
  for (std::size_t i = 0; i < 16; ++i) {
    CHECK(bytes[i] == ref.scene.bytes[i]);
    CHECK(bytes[16 + i] == ref.entity.bytes[i]);
  }

  const auto decoded = entityRefFromBytes(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value() == ref);
}

TEST_CASE("parseEntityRef rejects the wrong length", "[asset_system][entity_ref]") {
  requireParseError("", EntityRefParseError::WrongLength);
  requireParseError(std::string(kSceneText), EntityRefParseError::WrongLength);
  requireParseError(std::string(kSceneText) + "/" + kEntityText + "0", EntityRefParseError::WrongLength);
  requireParseError(std::string(kSceneText) + "/" + std::string(kEntityText).substr(1), EntityRefParseError::WrongLength);
}

TEST_CASE("parseEntityRef rejects a missing separator", "[asset_system][entity_ref]") {
  requireParseError(std::string(kSceneText) + ":" + kEntityText, EntityRefParseError::MissingSeparator);
  requireParseError(std::string(kSceneText) + "-" + kEntityText, EntityRefParseError::MissingSeparator);
  requireParseError(std::string(kSceneText) + " " + kEntityText, EntityRefParseError::MissingSeparator);
}

TEST_CASE("parseEntityRef reports a malformed GUID half with the GUID parser's own error",
          "[asset_system][entity_ref]") {
  // Scene half.
  std::string badScene = kSceneText;
  badScene[8] = 'x';
  requireParseError(badScene + "/" + kEntityText, EntityRefParseError::MissingHyphen);
  std::string upperScene = kSceneText;
  upperScene[0] = 'A';
  requireParseError(upperScene + "/" + kEntityText, EntityRefParseError::NotLowercaseHex);
  // Entity half.
  std::string badEntity = kEntityText;
  badEntity[13] = 'x';
  requireParseError(std::string(kSceneText) + "/" + badEntity, EntityRefParseError::MissingHyphen);
  std::string nonHexEntity = kEntityText;
  nonHexEntity[1] = 'g';
  requireParseError(std::string(kSceneText) + "/" + nonHexEntity, EntityRefParseError::NotLowercaseHex);
}

TEST_CASE("parseEntityRef rejects a nil GUID in either half", "[asset_system][entity_ref]") {
  const std::string nil = "00000000-0000-0000-0000-000000000000";
  requireParseError(nil + "/" + kEntityText, EntityRefParseError::NilGuid);
  requireParseError(std::string(kSceneText) + "/" + nil, EntityRefParseError::NilGuid);
}

TEST_CASE("entityRefFromBytes rejects a nil half and accepts anything else", "[asset_system][entity_ref]") {
  EntityRefBytes nilScene = entityRefToBytes(sampleRef());
  for (std::size_t i = 0; i < 16; ++i) nilScene[i] = std::byte{0};
  const auto sceneResult = entityRefFromBytes(nilScene);
  REQUIRE(sceneResult.isErr());
  CHECK(sceneResult.error() == EntityRefParseError::NilGuid);

  EntityRefBytes nilEntity = entityRefToBytes(sampleRef());
  for (std::size_t i = 16; i < 32; ++i) nilEntity[i] = std::byte{0};
  const auto entityResult = entityRefFromBytes(nilEntity);
  REQUIRE(entityResult.isErr());
  CHECK(entityResult.error() == EntityRefParseError::NilGuid);

  CHECK(entityRefFromBytes(EntityRefBytes{}).isErr());  // both halves nil
}

TEST_CASE("Two EntityRefs are equal only when scene and entity both match", "[asset_system][entity_ref]") {
  const EntityRef ref = sampleRef();
  EntityRef otherScene = ref;
  otherScene.scene = parseAssetGuid("0047aaaa-0000-4000-8000-000000000002").value();
  EntityRef otherEntity = ref;
  otherEntity.entity = parseEntityGuid("e68122c6-1bb2-8f1f-b185-358f58780b06").value();
  CHECK(ref == sampleRef());
  CHECK(ref != otherScene);
  CHECK(ref != otherEntity);
}

#include <atlantis/asset_system/entity_ref.h>

#include <algorithm>

namespace atlantis::asset_system {

namespace {

[[nodiscard]] EntityRefParseError fromGuidError(GuidParseError error) noexcept {
  switch (error) {
    case GuidParseError::WrongLength:
      return EntityRefParseError::WrongLength;
    case GuidParseError::MissingHyphen:
      return EntityRefParseError::MissingHyphen;
    case GuidParseError::NotLowercaseHex:
      return EntityRefParseError::NotLowercaseHex;
    case GuidParseError::NilGuid:
      return EntityRefParseError::NilGuid;
  }
  return EntityRefParseError::WrongLength;
}

constexpr std::size_t kGuidTextLength = 36;

}  // namespace

std::string_view toString(EntityRefParseError error) noexcept {
  switch (error) {
    case EntityRefParseError::WrongLength:
      return "WrongLength";
    case EntityRefParseError::MissingSeparator:
      return "MissingSeparator";
    case EntityRefParseError::MissingHyphen:
      return "MissingHyphen";
    case EntityRefParseError::NotLowercaseHex:
      return "NotLowercaseHex";
    case EntityRefParseError::NilGuid:
      return "NilGuid";
  }
  return "(unrecognized EntityRefParseError)";
}

atlantis::Result<EntityRef, EntityRefParseError> parseEntityRef(std::string_view text) {
  using ResultT = atlantis::Result<EntityRef, EntityRefParseError>;
  if (text.size() != kEntityRefTextLength) return ResultT::Err(EntityRefParseError::WrongLength);
  if (text[kGuidTextLength] != '/') return ResultT::Err(EntityRefParseError::MissingSeparator);

  const auto scene = parseAssetGuid(text.substr(0, kGuidTextLength));
  if (scene.isErr()) return ResultT::Err(fromGuidError(scene.error()));
  const auto entity = parseEntityGuid(text.substr(kGuidTextLength + 1));
  if (entity.isErr()) return ResultT::Err(fromGuidError(entity.error()));
  return ResultT::Ok(EntityRef{scene.value(), entity.value()});
}

std::string toString(const EntityRef& ref) { return toString(ref.scene) + "/" + toString(ref.entity); }

EntityRefBytes entityRefToBytes(const EntityRef& ref) noexcept {
  EntityRefBytes bytes{};
  std::copy(ref.scene.bytes.begin(), ref.scene.bytes.end(), bytes.begin());
  std::copy(ref.entity.bytes.begin(), ref.entity.bytes.end(), bytes.begin() + ref.scene.bytes.size());
  return bytes;
}

atlantis::Result<EntityRef, EntityRefParseError> entityRefFromBytes(const EntityRefBytes& bytes) {
  using ResultT = atlantis::Result<EntityRef, EntityRefParseError>;
  GuidBytes sceneBytes{};
  GuidBytes entityBytes{};
  std::copy(bytes.begin(), bytes.begin() + sceneBytes.size(), sceneBytes.begin());
  std::copy(bytes.begin() + sceneBytes.size(), bytes.end(), entityBytes.begin());
  const auto scene = assetGuidFromBytes(sceneBytes);
  if (scene.isErr()) return ResultT::Err(fromGuidError(scene.error()));
  const auto entity = entityGuidFromBytes(entityBytes);
  if (entity.isErr()) return ResultT::Err(fromGuidError(entity.error()));
  return ResultT::Ok(EntityRef{scene.value(), entity.value()});
}

}  // namespace atlantis::asset_system

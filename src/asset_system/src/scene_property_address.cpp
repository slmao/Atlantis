#include <atlantis/asset_system/scene_property_address.h>

#include <cstdint>

namespace atlantis::asset_system::scene {

namespace {

constexpr std::size_t kGuidTextLength = 36;
constexpr std::size_t kIdTextLength = 16;
constexpr std::size_t kFirstSeparator = kGuidTextLength;                      // 36
constexpr std::size_t kSecondSeparator = kFirstSeparator + 1 + kIdTextLength;  // 53

[[nodiscard]] std::string toHex(std::uint64_t value) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out(kIdTextLength, '0');
  for (std::size_t i = 0; i < kIdTextLength; ++i) {
    out[kIdTextLength - 1 - i] = kDigits[(value >> (4 * i)) & 0xfu];
  }
  return out;
}

[[nodiscard]] bool fromHex(std::string_view text, std::uint64_t& out) {
  out = 0;
  for (const char c : text) {
    std::uint64_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<std::uint64_t>(c - 'a' + 10);
    } else {
      return false;
    }
    out = (out << 4) | digit;
  }
  return true;
}

}  // namespace

std::string_view toString(PropertyAddressParseError error) noexcept {
  switch (error) {
    case PropertyAddressParseError::WrongLength: return "WrongLength";
    case PropertyAddressParseError::MissingSeparator: return "MissingSeparator";
    case PropertyAddressParseError::MalformedGuid: return "MalformedGuid";
    case PropertyAddressParseError::NilGuid: return "NilGuid";
    case PropertyAddressParseError::MalformedId: return "MalformedId";
    case PropertyAddressParseError::ZeroId: return "ZeroId";
  }
  return "Unknown";
}

std::string toString(const PropertyAddress& address) {
  return toString(address.node) + '/' + toHex(address.component.value) + '/' + toHex(address.field.value);
}

atlantis::Result<PropertyAddress, PropertyAddressParseError> parsePropertyAddress(std::string_view text) {
  using ResultT = atlantis::Result<PropertyAddress, PropertyAddressParseError>;
  if (text.size() != kPropertyAddressTextLength) return ResultT::Err(PropertyAddressParseError::WrongLength);
  if (text[kFirstSeparator] != '/' || text[kSecondSeparator] != '/') {
    return ResultT::Err(PropertyAddressParseError::MissingSeparator);
  }

  PropertyAddress address;
  const auto guid = parseEntityGuid(text.substr(0, kGuidTextLength));
  if (guid.isErr()) {
    return ResultT::Err(guid.error() == GuidParseError::NilGuid ? PropertyAddressParseError::NilGuid
                                                               : PropertyAddressParseError::MalformedGuid);
  }
  address.node = guid.value();

  std::uint64_t component = 0;
  std::uint64_t field = 0;
  if (!fromHex(text.substr(kFirstSeparator + 1, kIdTextLength), component) ||
      !fromHex(text.substr(kSecondSeparator + 1, kIdTextLength), field)) {
    return ResultT::Err(PropertyAddressParseError::MalformedId);
  }
  if (component == 0 || field == 0) return ResultT::Err(PropertyAddressParseError::ZeroId);
  address.component = schema::TypeId{component};
  address.field = schema::FieldId{field};
  return ResultT::Ok(address);
}

}  // namespace atlantis::asset_system::scene

#include <atlantis/asset_system/asset_guid.h>

#include "fnv1a64.h"

#include <algorithm>
#include <cstdint>

namespace atlantis::asset_system {

namespace {

constexpr std::size_t kTextLength = 36;
constexpr std::array<std::size_t, 4> kHyphenOffsets = {8, 13, 18, 23};

// FNV-1a-128 state as two 64-bit limbs. Multiplying by the prime
// 2^88 + 0x13b is h * 0x13b + (h << 88) mod 2^128; the shifted term only
// reaches the high limb, as lo << 24.
class Fnv1a128 {
 public:
  void update(std::span<const std::byte> data) noexcept {
    for (std::byte b : data) {
      lo_ ^= static_cast<std::uint64_t>(b);
      multiplyByPrime();
    }
  }

  [[nodiscard]] GuidBytes bigEndianBytes() const noexcept {
    GuidBytes out{};
    for (std::size_t i = 0; i < 8; ++i) {
      out[i] = static_cast<std::byte>((hi_ >> (56 - 8 * i)) & 0xFFU);
      out[8 + i] = static_cast<std::byte>((lo_ >> (56 - 8 * i)) & 0xFFU);
    }
    return out;
  }

 private:
  static constexpr std::uint64_t kPrimeLow = 0x13b;

  void multiplyByPrime() noexcept {
    // lo * kPrimeLow's high 64 bits, from 32-bit halves: each partial
    // product is below 2^41, so nothing overflows before the final shift.
    const std::uint64_t lowHalf = (lo_ & 0xFFFFFFFFULL) * kPrimeLow;
    const std::uint64_t highHalf = (lo_ >> 32) * kPrimeLow;
    const std::uint64_t carry = (highHalf + (lowHalf >> 32)) >> 32;
    const std::uint64_t newHi = hi_ * kPrimeLow + carry + (lo_ << 24);
    lo_ *= kPrimeLow;
    hi_ = newHi;
  }

  std::uint64_t hi_ = 0x6c62272e07bb0142ULL;
  std::uint64_t lo_ = 0x62b821756295c58dULL;
};

[[nodiscard]] bool isNil(const GuidBytes& bytes) noexcept {
  return std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{0}; });
}

[[nodiscard]] int hexValue(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

[[nodiscard]] atlantis::Result<GuidBytes, GuidParseError> parseBytes(std::string_view text) {
  using ResultT = atlantis::Result<GuidBytes, GuidParseError>;
  if (text.size() != kTextLength) return ResultT::Err(GuidParseError::WrongLength);
  for (std::size_t offset : kHyphenOffsets) {
    if (text[offset] != '-') return ResultT::Err(GuidParseError::MissingHyphen);
  }
  GuidBytes bytes{};
  std::size_t digitIndex = 0;
  for (std::size_t i = 0; i < kTextLength; ++i) {
    if (std::find(kHyphenOffsets.begin(), kHyphenOffsets.end(), i) != kHyphenOffsets.end()) continue;
    const int value = hexValue(text[i]);
    if (value < 0) return ResultT::Err(GuidParseError::NotLowercaseHex);
    std::byte& target = bytes[digitIndex / 2];
    target |= static_cast<std::byte>(digitIndex % 2 == 0 ? value << 4 : value);
    ++digitIndex;
  }
  if (isNil(bytes)) return ResultT::Err(GuidParseError::NilGuid);
  return ResultT::Ok(bytes);
}

[[nodiscard]] std::string formatBytes(const GuidBytes& bytes) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string text;
  text.reserve(kTextLength);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) text.push_back('-');
    const auto value = static_cast<unsigned>(bytes[i]);
    text.push_back(kHexDigits[value >> 4]);
    text.push_back(kHexDigits[value & 0xFU]);
  }
  return text;
}

[[nodiscard]] GuidBytes deriveBytes(const AssetGuid& owner, std::string_view subKey) noexcept {
  Fnv1a128 hash;
  hash.update(owner.bytes);
  hash.update(std::as_bytes(std::span<const char>(subKey.data(), subKey.size())));
  GuidBytes bytes = hash.bigEndianBytes();
  bytes[6] = (bytes[6] & std::byte{0x0F}) | std::byte{0x80};
  bytes[8] = (bytes[8] & std::byte{0x3F}) | std::byte{0x80};
  return bytes;
}

}  // namespace

atlantis::Result<AssetGuid, GuidParseError> parseAssetGuid(std::string_view text) {
  using ResultT = atlantis::Result<AssetGuid, GuidParseError>;
  auto parsed = parseBytes(text);
  if (parsed.isErr()) return ResultT::Err(parsed.error());
  return ResultT::Ok(AssetGuid{parsed.value()});
}

atlantis::Result<EntityGuid, GuidParseError> parseEntityGuid(std::string_view text) {
  using ResultT = atlantis::Result<EntityGuid, GuidParseError>;
  auto parsed = parseBytes(text);
  if (parsed.isErr()) return ResultT::Err(parsed.error());
  return ResultT::Ok(EntityGuid{parsed.value()});
}

std::string toString(const AssetGuid& guid) { return formatBytes(guid.bytes); }
std::string toString(const EntityGuid& guid) { return formatBytes(guid.bytes); }

atlantis::Result<AssetGuid, GuidParseError> assetGuidFromBytes(const GuidBytes& bytes) {
  using ResultT = atlantis::Result<AssetGuid, GuidParseError>;
  if (isNil(bytes)) return ResultT::Err(GuidParseError::NilGuid);
  return ResultT::Ok(AssetGuid{bytes});
}

atlantis::Result<EntityGuid, GuidParseError> entityGuidFromBytes(const GuidBytes& bytes) {
  using ResultT = atlantis::Result<EntityGuid, GuidParseError>;
  if (isNil(bytes)) return ResultT::Err(GuidParseError::NilGuid);
  return ResultT::Ok(EntityGuid{bytes});
}

GuidBytes fnv1a128(std::span<const std::byte> data) noexcept {
  Fnv1a128 hash;
  hash.update(data);
  return hash.bigEndianBytes();
}

AssetGuid deriveAssetGuid(const AssetGuid& owner, std::string_view subKey) noexcept {
  return AssetGuid{deriveBytes(owner, subKey)};
}

EntityGuid deriveEntityGuid(const AssetGuid& scene, std::string_view subKey) noexcept {
  return EntityGuid{deriveBytes(scene, subKey)};
}

AssetId assetKey(const AssetGuid& guid) noexcept {
  return detail::fnv1a64(guid.bytes);
}

}  // namespace atlantis::asset_system

#include <atlantis/asset_system/asset_id.h>

namespace atlantis::asset_system {

std::string toHexString(AssetId id) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string result(16, '0');
  for (int i = 15; i >= 0; --i) {
    result[static_cast<std::size_t>(i)] = kHexDigits[id & 0xFU];
    id >>= 4;
  }
  return result;
}

std::array<std::byte, 8> toLittleEndianBytes(AssetId id) noexcept {
  std::array<std::byte, 8> bytes{};
  for (std::size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<std::byte>((id >> (8 * i)) & 0xFFU);
  }
  return bytes;
}

AssetId fromLittleEndianBytes(const std::array<std::byte, 8>& bytes) noexcept {
  AssetId id = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    id |= static_cast<AssetId>(bytes[i]) << (8 * i);
  }
  return id;
}

}  // namespace atlantis::asset_system

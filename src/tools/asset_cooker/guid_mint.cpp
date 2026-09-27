#include "guid_mint.h"

#include <algorithm>
#include <random>

namespace atlantis::tools::asset_cooker {

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::GuidBytes;

AssetGuid mintAssetGuid(const std::function<std::uint32_t()>& draw) {
  GuidBytes bytes{};
  do {
    for (std::size_t word = 0; word < 4; ++word) {
      const std::uint32_t value = draw();
      for (std::size_t i = 0; i < 4; ++i) {
        bytes[4 * word + i] = static_cast<std::byte>((value >> (24 - 8 * i)) & 0xFFU);
      }
    }
  } while (std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{0}; }));
  bytes[6] = (bytes[6] & std::byte{0x0F}) | std::byte{0x40};
  bytes[8] = (bytes[8] & std::byte{0x3F}) | std::byte{0x80};
  return AssetGuid{bytes};
}

std::vector<AssetGuid> mintAssetGuids(std::size_t count) {
  std::random_device device;
  const std::function<std::uint32_t()> draw = [&device] { return static_cast<std::uint32_t>(device()); };
  std::vector<AssetGuid> guids;
  guids.reserve(count);
  for (std::size_t i = 0; i < count; ++i) guids.push_back(mintAssetGuid(draw));
  return guids;
}

}  // namespace atlantis::tools::asset_cooker

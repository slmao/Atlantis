#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::connection {
namespace assets {
enum class AssetType : std::uint8_t { Mesh, Texture, Material, Scene, Environment };
enum class AssetError : std::uint8_t { CatalogUnavailable, UnknownAsset };
[[nodiscard]] std::string_view toString(AssetType type) noexcept;
[[nodiscard]] std::string_view toString(AssetError error) noexcept;
struct AssetSummary {
  asset_system::AssetGuid guid;
  AssetType type = AssetType::Mesh;
  std::string source;
  friend bool operator==(const AssetSummary&, const AssetSummary&) = default;
};
struct AssetDetails {
  asset_system::AssetGuid guid;
  AssetType type = AssetType::Mesh;
  std::string source;
  std::uint32_t artifactSchema = 0;
  std::optional<std::uint32_t> sourceSchema;
  std::string tool;
  std::vector<asset_system::AssetGuid> dependencies;
  friend bool operator==(const AssetDetails&, const AssetDetails&) = default;
};
}  // namespace assets

// Spec 0081 / ADR-0143: read-only cooked-catalog facts, not GPU residency.
// Not thread-safe: the connection's owning thread (in-process host queries run
// on the frame thread between frames). Connections borrow their host and must
// close before it. Results are independent owned copies. For either query, a
// Remote transport failure returns Err(CatalogUnavailable) as a sentinel;
// RemoteSession::failure() is authoritative and must be checked first.
class AssetConnection {
 public:
  virtual ~AssetConnection() = default;
  [[nodiscard]] virtual Result<std::vector<assets::AssetSummary>, assets::AssetError> listAssets() const = 0;
  // Precondition: guid is non-nil.
  [[nodiscard]] virtual Result<assets::AssetDetails, assets::AssetError> getAsset(
      const asset_system::AssetGuid& guid) const = 0;
};
}  // namespace atlantis::connection

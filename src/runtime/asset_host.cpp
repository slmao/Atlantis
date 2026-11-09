#include "asset_host.h"
#include "asset_rebuild_host.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/log.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace atlantis::runtime {
namespace as = asset_system;
namespace ac = connection::assets;
struct AssetHost::State final : connection::AssetConnection {
  State(std::string path, AssetRebuildHost* rebuild) : path_(std::move(path)), rebuild_(rebuild) {}
  std::string path_;
  AssetRebuildHost* rebuild_;
  mutable std::shared_ptr<const as::AssetCatalog> catalog_;
  mutable bool initialized_ = false;
  mutable std::vector<ac::AssetDetails> details_;
  mutable std::vector<ac::AssetSummary> summaries_;
  mutable bool ready_ = false;

  void initialize() const {
    if (initialized_) return;
    initialized_ = true;
    auto loaded = as::loadAssetCatalog(path_);
    if (loaded.isErr()) {
      ATLANTIS_LOG_ERROR("assets: catalog unavailable ({})", as::toString(loaded.error()));
      return;
    }
    for (const auto& r : loaded.value().records()) {
      ac::AssetType type = ac::AssetType::Mesh;
      switch (r.type) {
        case as::CatalogAssetType::Mesh: type = ac::AssetType::Mesh; break;
        case as::CatalogAssetType::Texture: type = ac::AssetType::Texture; break;
        case as::CatalogAssetType::Material: type = ac::AssetType::Material; break;
        case as::CatalogAssetType::Scene: type = ac::AssetType::Scene; break;
        case as::CatalogAssetType::Environment: type = ac::AssetType::Environment; break;
        case as::CatalogAssetType::GltfImport: ATLANTIS_CHECK(false); break;
      }
      details_.push_back({r.guid, type, as::toString(r.source), r.artifactSchema, r.sourceSchema, r.tool, r.dependencies});
    }
    std::sort(details_.begin(), details_.end(), [](const auto& a, const auto& b) { return a.guid < b.guid; });
    for (const auto& d : details_) summaries_.push_back({d.guid, d.type, d.source});
    if (rebuild_) catalog_ = std::make_shared<const as::AssetCatalog>(std::move(loaded.value()));
    ready_ = true;
  }
  Result<std::vector<ac::AssetSummary>, ac::AssetError> listAssets() const override {
    initialize();
    using R = Result<std::vector<ac::AssetSummary>, ac::AssetError>;
    return ready_ ? R::Ok(summaries_) : R::Err(ac::AssetError::CatalogUnavailable);
  }
  Result<ac::AssetDetails, ac::AssetError> getAsset(const as::AssetGuid& guid) const override {
    ATLANTIS_CHECK(guid != as::AssetGuid{});
    initialize();
    using R = Result<ac::AssetDetails, ac::AssetError>;
    if (!ready_) return R::Err(ac::AssetError::CatalogUnavailable);
    const auto it = std::lower_bound(details_.begin(), details_.end(), guid,
                                    [](const auto& d, const auto& key) { return d.guid < key; });
    return it != details_.end() && it->guid == guid ? R::Ok(*it) : R::Err(ac::AssetError::UnknownAsset);
  }
  ac::RebuildStartResult rebuild(const as::AssetGuid& guid) override {
    if (!rebuild_) return ac::RebuildStartResult::Err(ac::RebuildError::Unavailable);
    if (auto error = rebuild_->admissionError()) return ac::RebuildStartResult::Err(*error);
    initialize();
    return rebuild_->start(guid, catalog_);
  }
  ac::RebuildStatusResult rebuildStatus(std::optional<ac::RebuildId> id) const override {
    return rebuild_ ? rebuild_->status(id) : ac::RebuildStatusResult::Err(ac::RebuildError::Unavailable);
  }
  ac::ImportStartResult importAsset(std::string_view name, ac::ImportMode mode) override {
    if (!rebuild_) return ac::ImportStartResult::Err(ac::ImportError::Unavailable);
    initialize();
    return rebuild_->startImport(name, mode, catalog_);
  }
  ac::ImportStatusResult importStatus(std::optional<ac::ImportId> id) const override {
    return rebuild_ ? rebuild_->importStatus(id) : ac::ImportStatusResult::Err(ac::ImportError::Unavailable);
  }
};
namespace {
class BorrowedAssets final : public connection::AssetConnection {
 public:
  explicit BorrowedAssets(connection::AssetConnection& host) : host_(host) {}
  Result<std::vector<ac::AssetSummary>, ac::AssetError> listAssets() const override { return host_.listAssets(); }
  Result<ac::AssetDetails, ac::AssetError> getAsset(const as::AssetGuid& guid) const override { return host_.getAsset(guid); }
  ac::RebuildStartResult rebuild(const as::AssetGuid& guid) override { return host_.rebuild(guid); }
  ac::RebuildStatusResult rebuildStatus(std::optional<ac::RebuildId> id) const override { return host_.rebuildStatus(id); }
  ac::ImportStartResult importAsset(std::string_view name, ac::ImportMode mode) override {
    return host_.importAsset(name, mode);
  }
  ac::ImportStatusResult importStatus(std::optional<ac::ImportId> id) const override {
    return host_.importStatus(id);
  }
 private:
  connection::AssetConnection& host_;
};
}
AssetHost::AssetHost(std::string path, AssetRebuildHost* rebuild)
    : state_(std::make_unique<State>(std::move(path), rebuild)) {}
AssetHost::~AssetHost() = default;
std::unique_ptr<connection::AssetConnection> AssetHost::connect() { return std::make_unique<BorrowedAssets>(*state_); }
}  // namespace atlantis::runtime

#include "asset_host.h"
#include "asset_rebuild_host.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/load_material.h>
#include <atlantis/asset_system/load_texture.h>
#include <atlantis/log.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace atlantis::runtime {
namespace as = asset_system;
namespace ac = connection::assets;
struct AssetHost::State final : connection::AssetConnection {
  State(std::string path, AssetRebuildHost* rebuild, PreviewProvider preview)
      : path_(std::move(path)), rebuild_(rebuild), preview_(std::move(preview)) {}
  std::string path_;
  AssetRebuildHost* rebuild_;
  PreviewProvider preview_;
  bool previewAdmitted_ = false;
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
    if (rebuild_ || preview_.render || preview_.renderPair) {
      catalog_ = std::make_shared<const as::AssetCatalog>(std::move(loaded.value()));
    }
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
  ac::MeshPreviewResult previewMesh(const as::AssetGuid& guid) override {
    using E = ac::PreviewError;
    using R = ac::MeshPreviewResult;
    ATLANTIS_CHECK(guid != as::AssetGuid{});
    if (!preview_.render) return R::Err(E::Unsupported);
    if (!preview_.available || !preview_.available()) return R::Err(E::Unavailable);
    if (std::exchange(previewAdmitted_, true)) return R::Err(E::Busy);
    initialize();
    if (!catalog_) return R::Err(E::CatalogUnavailable);
    const auto* record = catalog_->find(guid);
    if (!record) return R::Err(E::UnknownAsset);
    if (record->type != as::CatalogAssetType::Mesh) return R::Err(E::NotMesh);
    auto mesh = as::loadStaticMeshPreviewAsset(*record);
    if (mesh.isErr()) {
      return R::Err(mesh.error() == as::MeshPreviewLoadError::TooLarge ? E::TooLarge : E::InvalidMesh);
    }
    return preview_.render(guid, std::move(mesh.value()));
  }
  ac::MeshMaterialPreviewResult previewMeshMaterial(const as::AssetGuid& meshGuid,
                                                     const as::AssetGuid& materialGuid) override {
    using E = ac::MeshMaterialPreviewError;
    using R = ac::MeshMaterialPreviewResult;
    ATLANTIS_CHECK(meshGuid != as::AssetGuid{} && materialGuid != as::AssetGuid{});
    if (!preview_.renderPair) return R::Err(E::Unsupported);
    if (!preview_.available || !preview_.available()) return R::Err(E::Unavailable);
    if (std::exchange(previewAdmitted_, true)) return R::Err(E::Busy);
    initialize();
    if (!catalog_) return R::Err(E::CatalogUnavailable);

    const auto* meshRecord = catalog_->find(meshGuid);
    if (!meshRecord) return R::Err(E::UnknownMesh);
    if (meshRecord->type != as::CatalogAssetType::Mesh) return R::Err(E::NotMesh);
    const auto* materialRecord = catalog_->find(materialGuid);
    if (!materialRecord) return R::Err(E::UnknownMaterial);
    if (materialRecord->type != as::CatalogAssetType::Material) return R::Err(E::NotMaterial);
    auto material = as::loadPreviewMaterialAsset(*materialRecord);
    if (material.isErr()) {
      return R::Err(material.error() == as::PreviewArtifactLoadError::TooLarge ? E::TooLarge : E::InvalidMaterial);
    }
    const auto& mat = material.value();
    if (mat.kind != as::MaterialKind::PbrDirectLit || mat.alphaMode != as::MaterialAlphaMode::Opaque ||
        mat.normalMapTexture != 0 || mat.emissiveTexture != 0 ||
        std::ranges::any_of(mat.emissiveFactor, [](float value) { return value != 0.0f; })) {
      return R::Err(E::UnsupportedMaterial);
    }

    const auto* textureRecord = catalog_->find(mat.textureAsset);
    const auto& dependencies = materialRecord->dependencies;
    if (!textureRecord || textureRecord->type != as::CatalogAssetType::Texture || !textureRecord->dependencies.empty() ||
        dependencies.size() != 1 || dependencies.front() != textureRecord->guid) {
      return R::Err(E::DependencyUnavailable);
    }
    auto mesh = as::loadStaticMeshPreviewAsset(*meshRecord);
    if (mesh.isErr()) {
      return R::Err(mesh.error() == as::MeshPreviewLoadError::TooLarge ? E::TooLarge : E::InvalidMesh);
    }
    auto texture = as::loadPreviewBaseColorTexture(*textureRecord);
    if (texture.isErr()) {
      return R::Err(texture.error() == as::PreviewArtifactLoadError::TooLarge ? E::TooLarge : E::DependencyUnavailable);
    }
    if (texture.value().colorSpace != as::TextureColorSpace::Srgb || texture.value().layout != as::TextureDataLayout::Rgba8) {
      return R::Err(E::UnsupportedMaterial);
    }
    return preview_.renderPair(meshGuid, materialGuid, mesh.value(), mat, texture.value());
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
  ac::MeshPreviewResult previewMesh(const as::AssetGuid& guid) override { return host_.previewMesh(guid); }
  ac::MeshMaterialPreviewResult previewMeshMaterial(const as::AssetGuid& meshGuid,
                                                    const as::AssetGuid& materialGuid) override {
    return host_.previewMeshMaterial(meshGuid, materialGuid);
  }
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
AssetHost::AssetHost(std::string path, AssetRebuildHost* rebuild, PreviewProvider preview)
    : state_(std::make_unique<State>(std::move(path), rebuild, std::move(preview))) {}
AssetHost::~AssetHost() = default;
std::unique_ptr<connection::AssetConnection> AssetHost::connect() { return std::make_unique<BorrowedAssets>(*state_); }
void AssetHost::beginServiceInterval() noexcept { state_->previewAdmitted_ = false; }
}  // namespace atlantis::runtime

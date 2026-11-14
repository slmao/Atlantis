#include "asset_host.h"
#include "asset_rebuild_host.h"
#include "material_authoring.h"

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
    catalog_ = std::make_shared<const as::AssetCatalog>(std::move(loaded.value()));
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
    if (mat.kind != as::MaterialKind::PbrDirectLit || mat.alphaMode != as::MaterialAlphaMode::Opaque)
      return R::Err(E::UnsupportedMaterial);
    const auto values = material_authoring::cookedValues(*catalog_, *materialRecord, mat);
    if (!values) return R::Err(E::DependencyUnavailable);
    auto mesh = as::loadStaticMeshPreviewAsset(*meshRecord);
    if (mesh.isErr()) {
      return R::Err(mesh.error() == as::MeshPreviewLoadError::TooLarge ? E::TooLarge : E::InvalidMesh);
    }
    auto textures = material_authoring::textures(*catalog_, *values);
    if (textures.isErr()) {
      using M = ac::MaterialError;
      if (textures.error() == M::TooLarge) return R::Err(E::TooLarge);
      if (textures.error() == M::UnsupportedMaterial) return R::Err(E::UnsupportedMaterial);
      return R::Err(E::DependencyUnavailable);
    }
    const auto& maps = textures.value();
    return preview_.renderPair(meshGuid, materialGuid, mesh.value(), mat, maps.base,
                               maps.normal ? &*maps.normal : nullptr, maps.emissive ? &*maps.emissive : nullptr);
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
  ac::TextureImportStartResult textureImport(std::string_view name, ac::ImportMode mode) override {
    if (!rebuild_) return ac::TextureImportStartResult::Err(ac::ImportError::Unavailable);
    initialize();
    return rebuild_->startTextureImport(name, mode, catalog_);
  }
  ac::TextureImportStatusResult textureImportStatus(std::optional<ac::TextureImportId> id) const override {
    return rebuild_ ? rebuild_->textureImportStatus(id) : ac::TextureImportStatusResult::Err(ac::ImportError::Unavailable);
  }
  ac::TextureImportStartResult normalMapImport(std::string_view name, ac::ImportMode mode) override {
    if (!rebuild_) return ac::TextureImportStartResult::Err(ac::ImportError::Unavailable);
    initialize();
    return rebuild_->startTextureImport(name, mode, catalog_, AssetRebuildHost::TextureKind::Normal);
  }
  ac::TextureImportStatusResult normalMapImportStatus(std::optional<ac::TextureImportId> id) const override {
    return rebuild_ ? rebuild_->textureImportStatus(id, AssetRebuildHost::TextureKind::Normal)
                    : ac::TextureImportStatusResult::Err(ac::ImportError::Unavailable);
  }
  ac::MaterialReadResult materialRead(const as::AssetGuid& guid) const override {
    return readMaterial(guid, ac::MaterialRecipeVersion::Basic);
  }
  ac::MaterialReadResult materialReadWithMaps(const as::AssetGuid& guid) const override {
    return readMaterial(guid, ac::MaterialRecipeVersion::WithMaps);
  }
  ac::MaterialReadResult readMaterial(const as::AssetGuid& guid, ac::MaterialRecipeVersion version) const {
    using R = ac::MaterialReadResult;
    initialize();
    if (!catalog_) return R::Err(ac::MaterialError::CatalogUnavailable);
    auto recipe = material_authoring::read(*catalog_, guid, version);
    if (recipe.isErr()) return R::Err(recipe.error());
    return R::Ok({guid, recipe.value(), rebuild_ && rebuild_->ownsMaterial(guid),
                  rebuild_ ? rebuild_->activeGeneration() : std::nullopt});
  }
  ac::MaterialSaveResult materialSave(const ac::MaterialSave& request) override {
    if (!rebuild_) return ac::MaterialSaveResult::Err(ac::MaterialError::Unavailable);
    initialize();
    return rebuild_->saveMaterial(request, catalog_);
  }
  ac::MaterialSaveResult materialSaveWithMaps(const ac::MaterialSave& request) override {
    if (!rebuild_) return ac::MaterialSaveResult::Err(ac::MaterialError::Unavailable);
    initialize();
    return rebuild_->saveMaterial(request, catalog_, ac::MaterialRecipeVersion::WithMaps);
  }
  ac::MaterialStatusResult materialStatus(std::optional<ac::MaterialJobId> id) const override {
    return rebuild_ ? rebuild_->materialStatus(id) : ac::MaterialStatusResult::Err(ac::MaterialError::Unavailable);
  }
  ac::MaterialDraftPreviewResult previewMaterialDraft(const as::AssetGuid& guid,
                                                     const ac::MaterialRecipe& recipe) override {
    if (!ac::basicMaterialValues(recipe.values))
      return ac::MaterialDraftPreviewResult::Err(ac::MaterialError::InvalidRecipe);
    return previewMaterialDraftWithMaps(guid, recipe);
  }
  ac::MaterialDraftPreviewResult previewMaterialDraftWithMaps(const as::AssetGuid& guid,
      const ac::MaterialRecipe& recipe) override {
    using E = ac::MaterialError;
    using R = ac::MaterialDraftPreviewResult;
    if (guid == as::AssetGuid{} || !ac::validMaterialRecipe(recipe)) return R::Err(E::InvalidRecipe);
    if (!preview_.renderPair) return R::Err(E::Unsupported);
    if (!preview_.available || !preview_.available()) return R::Err(E::Unavailable);
    if (std::exchange(previewAdmitted_, true)) return R::Err(E::Busy);
    initialize();
    if (!catalog_) return R::Err(E::CatalogUnavailable);
    const auto* record = catalog_->find(guid);
    if (!record) return R::Err(E::UnknownMesh);
    if (record->type != as::CatalogAssetType::Mesh) return R::Err(E::NotMesh);
    auto mesh = as::loadStaticMeshPreviewAsset(*record);
    if (mesh.isErr())
      return R::Err(mesh.error() == as::MeshPreviewLoadError::TooLarge ? E::TooLarge : E::InvalidMesh);
    auto textures = material_authoring::textures(*catalog_, recipe.values);
    if (textures.isErr()) return R::Err(textures.error());
    const auto& maps = textures.value();
    // The pair renderer uses the ID for request-local resource labels only.
    // A draft has no persistent material identity.
    auto image = preview_.renderPair(guid, {}, mesh.value(), material_authoring::previewData(recipe), maps.base,
                                     maps.normal ? &*maps.normal : nullptr,
                                     maps.emissive ? &*maps.emissive : nullptr);
    if (image.isOk()) return R::Ok({guid, recipe, std::move(image.value().png)});
    using Pair = ac::MeshMaterialPreviewError;
    if (image.error() == Pair::Unavailable) return R::Err(E::Unavailable);
    if (image.error() == Pair::IncompatiblePair) return R::Err(E::IncompatiblePair);
    return R::Err(E::RenderFailed);
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
  ac::TextureImportStartResult textureImport(std::string_view name, ac::ImportMode mode) override {
    return host_.textureImport(name, mode);
  }
  ac::TextureImportStatusResult textureImportStatus(std::optional<ac::TextureImportId> id) const override {
    return host_.textureImportStatus(id);
  }
  ac::TextureImportStartResult normalMapImport(std::string_view name, ac::ImportMode mode) override {
    return host_.normalMapImport(name, mode);
  }
  ac::TextureImportStatusResult normalMapImportStatus(std::optional<ac::TextureImportId> id) const override {
    return host_.normalMapImportStatus(id);
  }
  ac::MaterialReadResult materialRead(const as::AssetGuid& guid) const override { return host_.materialRead(guid); }
  ac::MaterialSaveResult materialSave(const ac::MaterialSave& request) override { return host_.materialSave(request); }
  ac::MaterialStatusResult materialStatus(std::optional<ac::MaterialJobId> id) const override {
    return host_.materialStatus(id);
  }
  ac::MaterialDraftPreviewResult previewMaterialDraft(const as::AssetGuid& guid,
                                                     const ac::MaterialRecipe& recipe) override {
    return host_.previewMaterialDraft(guid, recipe);
  }
  ac::MaterialReadResult materialReadWithMaps(const as::AssetGuid& guid) const override {
    return host_.materialReadWithMaps(guid);
  }
  ac::MaterialSaveResult materialSaveWithMaps(const ac::MaterialSave& request) override {
    return host_.materialSaveWithMaps(request);
  }
  ac::MaterialDraftPreviewResult previewMaterialDraftWithMaps(const as::AssetGuid& guid,
      const ac::MaterialRecipe& recipe) override {
    return host_.previewMaterialDraftWithMaps(guid, recipe);
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

#include "authoring_host.h"
#include "asset_host.h"
#include "scene_save_as.h"
#include "scene_recovery_store.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/log.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace atlantis::runtime {

namespace {

namespace as = atlantis::asset_system;

// ADR-0129 D3: the document is the Runtime's scene, found through its catalog
// entry -- its own `scene` entry, or the `gltf_import` entry it was made from.
[[nodiscard]] std::optional<atlantis::authoring::DocumentSource> locateRuntimeScene(const AuthoringSources& sources,
                                                                                   const as::AssetGuid& scene) {
  std::ifstream in(sources.catalogSource, std::ios::binary);
  if (!in) {
    ATLANTIS_LOG_ERROR("authoring: cannot read the catalog source {}", sources.catalogSource.string());
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto catalog = as::parseAssetCatalogSource(text);
  if (catalog.isErr()) {
    ATLANTIS_LOG_ERROR("authoring: the catalog source {} does not parse", sources.catalogSource.string());
    return std::nullopt;
  }
  const as::CatalogSourceEntry* entry = catalog.value().find(scene);
  if (entry == nullptr) {
    for (const atlantis::authoring::ImportedScene& imported : sources.roots.imports) {
      if (imported.scene == scene) entry = catalog.value().find(imported.importEntry);
    }
  }
  if (entry == nullptr) {
    ATLANTIS_LOG_ERROR("authoring: the Runtime's scene {} has no catalog source entry", as::toString(scene));
    return std::nullopt;
  }
  auto located = atlantis::authoring::locateDocument(*entry, sources.roots);
  if (located.isErr()) {
    ATLANTIS_LOG_ERROR("authoring: the Runtime's scene cannot be opened ({})",
                       atlantis::authoring::toString(located.error()));
    return std::nullopt;
  }
  return std::move(located.value());
}

}  // namespace

AuthoringHost::AuthoringHost(RuntimeApplication& application, AuthoringSources sources)
    : application_(application), sources_(std::move(sources)) {}

AuthoringHost::~AuthoringHost() = default;

std::unique_ptr<atlantis::connection::AuthoringConnection> AuthoringHost::connect() {
  if (!session_ && !unavailable_) {
    auto source = locateRuntimeScene(sources_, application_.sceneGuid());
    if (!source) {
      unavailable_ = true;
      return nullptr;
    }
    session_ = std::make_unique<atlantis::authoring::AuthoringSession>(std::move(*source));
    if (const auto* hand = std::get_if<authoring::HandAuthored>(&session_->source().provenance)) {
      recovery_ = std::make_unique<SceneRecoveryStore>(hand->sourcePath, session_->source().scene);
      session_->setRecoveryStore(*recovery_);
    }
    session_->setSceneSink([this](connection::authoring::ApplyId id, as::ValidatedSceneData scene) {
      return application_.requestSceneReload(id, std::move(scene));
    });
    if (sources_.allowSceneSaveAs && runningFromSceneDevelopmentBuild(ATLANTIS_RUNTIME_DEVELOPMENT_DIR)) {
      session_->setSaveAsSink([this](std::string_view name, std::string source, std::uint64_t revision) {
        return publishSceneCopy(sources_.roots.assetsRoot, sources_.catalogSource, sources_.cookedCatalog,
                                name, std::move(source), revision);
      });
      session_->setCreateSceneSink([this](std::string_view name, as::AssetGuid scene, std::string source) {
        return publishSceneStarter(sources_.roots.assetsRoot, sources_.catalogSource, sources_.cookedCatalog,
                                   name, scene, std::move(source));
      });
    }
    if (sources_.assets && std::holds_alternative<authoring::HandAuthored>(session_->source().provenance) &&
        runningFromSceneDevelopmentBuild(ATLANTIS_RUNTIME_DEVELOPMENT_DIR)) {
      session_->setPrefabLookup([this](const as::AssetGuid& guid) {
        return sources_.assets->connect()->prefab(guid).isOk();
      });
      session_->setPrefabSync([this](const as::AssetGuid& guid) -> std::optional<authoring::PrefabDefinition> {
        const auto* frozen = sources_.assets->frozenPrefab(guid);
        if (!frozen) return std::nullopt;
        return authoring::PrefabDefinition{frozen->group.name, frozen->source, frozen->revision};
      }, &digestSceneBytes, [this](const as::scene::Renderable& value) {
        const auto catalog = sources_.assets->catalog();
        if (!catalog) return false;
        const auto* mesh = catalog->find(value.meshAsset);
        if (!mesh || mesh->type != as::CatalogAssetType::Mesh) return false;
        if (!value.materialAsset) return true;
        const auto* material = catalog->find(*value.materialAsset);
        return material && material->type == as::CatalogAssetType::Material;
      });
      if (sources_.allowPrefabRegistration) {
        session_->setPrefabUpdateSink([this](as::AssetGuid guid, std::string_view expected, std::string source,
                                             std::uint64_t revision) {
          using namespace connection::authoring;
          const auto* frozen = sources_.assets->frozenPrefab(guid);
          const auto snapshot = sources_.assets->catalog();
          if (!frozen || !snapshot || frozen->revision != expected)
            return PrefabUpdateResult::Err(SaveAsError::StaleSource);
          return replacePrefabSource(sources_.roots.assetsRoot, sources_.catalogSource, sources_.cookedCatalog,
              frozen->group.name, guid, frozen->source, frozen->revision, std::move(source), revision, *snapshot);
        });
        session_->setPrefabSink([this](std::string_view name, as::AssetGuid guid, std::string source,
                                       std::uint64_t revision) {
          const auto snapshot = sources_.assets->catalog();
          if (!snapshot) {
            using namespace connection::authoring;
            return Result<PrefabRegistration, SaveAsError>::Err(SaveAsError::WriteFailed);
          }
          return publishPrefab(sources_.roots.assetsRoot, sources_.catalogSource, sources_.cookedCatalog,
                               name, guid, std::move(source), revision, *snapshot);
        });
      }
    }
    ATLANTIS_LOG_INFO("authoring: session created for scene {}", as::toString(application_.sceneGuid()));
  }
  if (!session_) return nullptr;
  return session_->connect();
}

void AuthoringHost::afterFrame() {
  if (recovery_) recovery_->settle();
  if (auto result = application_.takeSceneReloadResult(); result && session_)
    session_->reportReload(result->first, std::move(result->second));
}

}  // namespace atlantis::runtime

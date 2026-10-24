#include "authoring_host.h"

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
    session_->setSceneSink([this](as::ValidatedSceneData scene) {
      return application_.requestSceneReload(std::move(scene));
    });
    generation_ = application_.sceneGeneration();
    ATLANTIS_LOG_INFO("authoring: session created for scene {}", as::toString(application_.sceneGuid()));
  }
  if (!session_) return nullptr;
  return session_->connect();
}

void AuthoringHost::afterFrame() {
  if (!session_ || application_.sceneGeneration() == generation_) return;
  generation_ = application_.sceneGeneration();
  session_->reportApplied(generation_);
}

}  // namespace atlantis::runtime

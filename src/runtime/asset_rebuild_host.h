#pragma once

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/connection/asset_connection.h>

#include <chrono>
#include <filesystem>
#include <memory>
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
#include <functional>
#endif

namespace atlantis::runtime {

// Executable-private Windows coordinator. Admission and status are called
// on the frame thread. Only the worker mutates staging or publishes outputs.
class AssetRebuildHost {
 public:
  enum class TextureKind { Color, Normal };
  struct Config {
    std::filesystem::path profile;
    std::filesystem::path seedCatalog;
    std::filesystem::path cmake;
    std::string configuration;
    std::optional<connection::assets::GenerationId> generation;
    std::chrono::steady_clock::duration timeout = std::chrono::minutes(30);
#ifdef ATLANTIS_REBUILD_TEST_CMAKE
    std::function<void(const std::filesystem::path&)> afterMaterialPreparation{};
#endif
  };

  [[nodiscard]] static Result<std::unique_ptr<AssetRebuildHost>, std::string> open(Config config);
  ~AssetRebuildHost();
  AssetRebuildHost(const AssetRebuildHost&) = delete;
  AssetRebuildHost& operator=(const AssetRebuildHost&) = delete;

  [[nodiscard]] const std::filesystem::path& catalogPath() const noexcept;
  [[nodiscard]] std::filesystem::path importedSource(asset_system::AssetGuid importRoot) const;
  [[nodiscard]] std::optional<connection::assets::RebuildError> admissionError() const;
  [[nodiscard]] connection::assets::RebuildStartResult start(
      asset_system::AssetGuid asset, std::shared_ptr<const asset_system::AssetCatalog> catalog);
  [[nodiscard]] connection::assets::RebuildStatusResult status(
      std::optional<connection::assets::RebuildId> id) const;
  [[nodiscard]] std::filesystem::path generationCatalogSource() const;
  [[nodiscard]] connection::assets::ImportStartResult startImport(
      std::string_view name, connection::assets::ImportMode mode,
      std::shared_ptr<const asset_system::AssetCatalog> catalog);
  [[nodiscard]] connection::assets::ImportStatusResult importStatus(std::optional<connection::assets::ImportId> id) const;
  [[nodiscard]] connection::assets::TextureImportStartResult startTextureImport(
      std::string_view name, connection::assets::ImportMode mode,
      std::shared_ptr<const asset_system::AssetCatalog> catalog, TextureKind kind = TextureKind::Color);
  [[nodiscard]] connection::assets::TextureImportStatusResult textureImportStatus(
      std::optional<connection::assets::TextureImportId> id, TextureKind kind = TextureKind::Color) const;
  [[nodiscard]] bool ownsMaterial(const asset_system::AssetGuid& guid) const;
  [[nodiscard]] std::optional<connection::assets::GenerationId> activeGeneration() const;
  [[nodiscard]] connection::assets::MaterialSaveResult saveMaterial(
      const connection::assets::MaterialSave& request, std::shared_ptr<const asset_system::AssetCatalog> catalog,
      connection::assets::MaterialRecipeVersion version = connection::assets::MaterialRecipeVersion::Basic);
  [[nodiscard]] connection::assets::MaterialStatusResult materialStatus(
      std::optional<connection::assets::MaterialJobId> id) const;
  void stop();

 private:
  struct State;
  explicit AssetRebuildHost(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};

}  // namespace atlantis::runtime

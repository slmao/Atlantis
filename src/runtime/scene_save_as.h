#pragma once

#include <atlantis/connection/authoring_connection.h>

#include <filesystem>

namespace atlantis::runtime {

bool runningFromSceneDevelopmentBuild(const std::filesystem::path& runtimeDirectory);

// Windows executable-private source publication. Catalog paths are selected
// by the host, and the only request-controlled value is the logical name.
Result<connection::authoring::SavedScene, connection::authoring::SaveAsError> publishSceneCopy(
    const std::filesystem::path& assetsRoot, const std::filesystem::path& catalogSource,
    const std::filesystem::path& cookedCatalog, std::string_view name, std::string source,
    std::uint64_t documentRevision);

Result<connection::authoring::CreatedScene, connection::authoring::CreateSceneError> publishSceneStarter(
    const std::filesystem::path& assetsRoot, const std::filesystem::path& catalogSource,
    const std::filesystem::path& cookedCatalog, std::string_view name,
    asset_system::AssetGuid scene, std::string source);

}  // namespace atlantis::runtime

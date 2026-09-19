#pragma once

#include "import_command.h"

#include <atlantis/result.h>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

struct cgltf_data;

// Plan 0037 Milestone 4 (material + texture slice). Internal to
// atlantis_gltf_importer_lib; forward-declares cgltf_data so no header
// includes cgltf.h (ADR-0084 boundary test).
namespace atlantis::gltf_importer::detail {

// Everything that can make the material slice fail is checked here, before
// any output exists: extension scope, texture features, sampler wraps,
// texture sources and their files on disk.
[[nodiscard]] atlantis::Result<std::monostate, GltfImportError> checkMaterials(
    const cgltf_data& data, const std::filesystem::path& contentRoot);

// Writes <stagingDir>/<name>/materials/<i>.material.txt for every glTF
// material, the white fallback texture when needed, appends
// cook_manifest.txt lines (textures first, then materials) and
// import_report.txt lines, and fills the summary's material fields.
[[nodiscard]] atlantis::Result<std::monostate, GltfImportError> writeMaterials(
    const cgltf_data& data, const std::filesystem::path& contentRoot, const std::filesystem::path& stagingDir,
    const std::string& name, GltfImportSummary& summary, std::vector<std::string>& reportLines,
    std::vector<std::string>& manifestLines);

// Logical path of material <index>, as the scene slice (Milestone 5) will
// reference it: "<name>/materials/<index>.material.txt".
[[nodiscard]] std::string materialLogicalPath(const std::string& name, std::size_t index);

// The 4x4 solid-white BC7 DDS written for materials without a base-color
// texture (Plan 0037 Ruling 7): DX10 header, DXGI 99 BC7_UNORM, one mode-6
// block. Exposed for tests.
[[nodiscard]] std::vector<unsigned char> whiteFallbackDds();

}  // namespace atlantis::gltf_importer::detail

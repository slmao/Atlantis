#pragma once

#include "import_command.h"

#include <atlantis/asset_system/scene_source.h>
#include <atlantis/result.h>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

struct cgltf_data;

// Plan 0037 Milestone 5 (scene-graph slice, ADR-0083 D5/D6). Internal to
// atlantis_gltf_importer_lib; forward-declares cgltf_data so no header
// includes cgltf.h (ADR-0084 boundary test).
namespace atlantis::gltf_importer::detail {

// Point lights need a positive range= in .scene.txt v4 (scene_source.cpp),
// but glTF's range is discarded by Plan 0037 Ruling 8; every imported point
// light gets this placeholder, and import_report.txt says so. glTF's own
// default range is infinite, so a large value approximates "no cutoff"
// (human ruling 2026-09-20); the real attenuation model belongs to Spec 0036
// workflow 2. Atlantis attenuates linearly to zero at `range`.
inline constexpr float kImportedPointLightRangePlaceholder = 10000.0f;

// Everything that can make the scene slice fail, checked before any output
// exists: node transforms, light types and values, and the light caps.
// Plan 0046 Milestone 2 (ADR-0094 Decision 3): with an overlay, also its
// own rules (no renderable node, at most one camera, parents only inside
// it), and the light caps over both together.
[[nodiscard]] atlantis::Result<std::monostate, GltfImportError> checkScene(
    const cgltf_data& data, const atlantis::asset_system::ParsedSceneSource* overlay);

// Writes <stagingDir>/<name>/<name>.scene.txt (the default scene, else
// scenes[0]), appends its cook_manifest.txt line and import_report.txt
// lines, and fills the summary's scene fields.
[[nodiscard]] atlantis::Result<std::monostate, GltfImportError> writeScene(
    const cgltf_data& data, const std::filesystem::path& stagingDir, const std::string& name,
    GltfImportSummary& summary, std::vector<std::string>& reportLines, std::vector<std::string>& manifestLines,
    const atlantis::asset_system::ParsedSceneSource* overlay);

// Logical path of mesh primitive (meshIndex, primitiveIndex) -- the one
// definition both the mesh slice and the scene slice use (human ruling
// 2026-09-19: meshes/<name>/mesh_<i>_<j>).
[[nodiscard]] std::string meshLogicalPath(const std::string& name, std::size_t meshIndex, std::size_t primitiveIndex);

}  // namespace atlantis::gltf_importer::detail

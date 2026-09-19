#pragma once

#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace atlantis::gltf_importer {

// Plan 0037 Milestone 3 (mesh slice). Four error categories per the
// Plan's own taxonomy; recoverable Result errors throughout, no
// exceptions. The importer is the sole producer of .amesh schema 5
// (uint32 indices) artifacts; each glTF primitive becomes exactly one output
// pair (<name>_mesh_<i>_<j>.amesh + .amesh.meta.txt) plus one import_report.txt.
enum class GltfImportError {
  // 1. Input/parse.
  InputUnreadable,
  MalformedGltf,  // cgltf parse or cgltf_validate failure not classified below
  BufferUnreadable,
  // 2. Unsupported content (Plan rulings: named, never silently skipped).
  NonTrianglesMode,
  NonIndexedPrimitive,
  MissingPosition,
  MissingRequiredAttribute,  // NORMAL or TEXCOORD_0 absent (Plan 0037 Ruling 9)
  UnsupportedIndexComponentType,
  // 3. Value validation.
  OutOfRangeIndex,
  OutOfRangeAccessor,     // accessor reaches past its bufferView, or bufferView past its buffer
  AttributeCountMismatch,  // a primitive's attribute accessors disagree on vertex count
  NonFiniteVertex,
  NonUnitNormal,
  TangentGenerationFailed,  // handedness conflict left after the split (a split defect, never expected)
  // 4. Output I/O.
  OutputDirectoryNotEmpty,
  OutputWriteFailed,
};

struct GltfImportSummary {
  std::uint32_t meshCount = 0;
  std::uint64_t totalVertices = 0;  // after the handedness split
  std::uint64_t totalIndices = 0;
  std::uint32_t meshesOverU16Range = 0;  // Spec 0036 workflow 1b's input census
  // Handedness split (human ruling 2026-09-19): vertices duplicated so no
  // vertex mixes tangent handedness across its faces (ADR-0073's generator
  // contract, unchanged).
  std::uint64_t splitVertices = 0;
  std::uint32_t meshesSplit = 0;
  double maxSplitGrowth = 0.0;  // largest splitVertices / source vertex count of one primitive
  std::string maxSplitGrowthMesh;
  // ADR-0073 Amendment 2026-09-19: vertices whose accumulated tangent was
  // degenerate (parallel to the normal or cancelled) and took the axis fallback.
  std::uint64_t degenerateFallbackVertices = 0;
  std::uint32_t meshesWithDegenerateFallback = 0;
  std::vector<std::string> reportLines;  // import_report.txt body (mesh slice)
};

// inputPath: the .gltf file. contentRoot: directory against which relative
// buffer URIs resolve (Plan's --content-root; Bistro's bistro.bin sits next
// to the .gltf). All output is written to a sibling staging directory and
// renamed to outputDir only after every file is written, so a failed import
// leaves no output behind. outputDir must be absent or empty.
[[nodiscard]] atlantis::Result<GltfImportSummary, GltfImportError> importGltfMeshes(
    const std::filesystem::path& inputPath, const std::filesystem::path& contentRoot,
    const std::filesystem::path& outputDir, const std::string& name);

[[nodiscard]] const char* gltfImportErrorMessage(GltfImportError error) noexcept;

}  // namespace atlantis::gltf_importer

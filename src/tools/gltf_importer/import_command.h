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
// (uint32 indices) artifacts; each glTF mesh becomes exactly one output
// pair (<name>_mesh_<i>.amesh + .amesh.meta.txt) plus one import_report.txt.
enum class GltfImportError {
  // 1. Input/parse.
  InputUnreadable,
  MalformedGltf,
  BufferUnreadable,
  // 2. Unsupported content (Plan rulings: named, never silently skipped).
  NonTrianglesMode,
  NonIndexedPrimitive,
  MissingPosition,
  UnsupportedIndexComponentType,
  // 3. Value validation.
  OutOfRangeIndex,
  OutOfRangeAccessor,
  NonFiniteVertex,
  NonUnitNormal,
  TangentGenerationFailed,  // degenerate basis or whole-mesh handedness conflict (ADR-0073)
  // 4. Output I/O.
  OutputWriteFailed,
};

struct GltfImportSummary {
  std::uint32_t meshCount = 0;
  std::uint64_t totalVertices = 0;
  std::uint64_t totalIndices = 0;
  std::uint32_t meshesOverU16Range = 0;  // Spec 0036 workflow 1b's input census
  std::vector<std::string> reportLines;  // import_report.txt body (mesh slice)
};

// inputPath: the .gltf file. contentRoot: directory against which relative
// buffer URIs resolve (Plan's --content-root; Bistro's bistro.bin sits next
// to the .gltf). outputDir is created on demand; a failed import never
// leaves output behind.
[[nodiscard]] atlantis::Result<GltfImportSummary, GltfImportError> importGltfMeshes(
    const std::filesystem::path& inputPath, const std::filesystem::path& contentRoot,
    const std::filesystem::path& outputDir, const std::string& name);

[[nodiscard]] const char* gltfImportErrorMessage(GltfImportError error) noexcept;

}  // namespace atlantis::gltf_importer

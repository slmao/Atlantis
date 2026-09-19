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
  MissingTextureFile,  // a referenced texture's file is absent under the content root
  // 2. Unsupported content (Plan rulings: named, never silently skipped).
  NonTrianglesMode,
  NonIndexedPrimitive,
  MissingPosition,
  MissingRequiredAttribute,  // NORMAL or TEXCOORD_0 absent (Plan 0037 Ruling 9)
  UnsupportedIndexComponentType,
  UnsupportedMaterialExtension,  // a material extension outside Spec 0037's three
  UnsupportedTextureFeature,     // texCoord != 0, KHR_texture_transform, or one PNG/JPG as both colour and data
  UnsupportedSamplerWrap,        // MIRRORED_REPEAT, or wrapS != wrapT (v6 has one address mode)
  TextureWithoutSource,          // neither an MSFT_texture_dds nor a usable core image URI
  UnsupportedLightType,          // KHR_lights_punctual spot light (Plan 0037 Ruling 8)
  // 3. Value validation.
  OutOfRangeIndex,
  OutOfRangeAccessor,     // accessor reaches past its bufferView, or bufferView past its buffer
  AttributeCountMismatch,  // a primitive's attribute accessors disagree on vertex count
  NonFiniteVertex,
  NonUnitNormal,
  InvalidMaterialFactor,  // a used material factor is non-finite or outside [0, 1]
  InvalidNodeTransform,   // non-finite TRS value or a zero-length rotation quaternion
  NonDecomposableMatrix,  // node matrix non-finite, non-affine, zero-scale or sheared
  NegativeDeterminant,    // node transform mirrors (odd number of negative scale factors)
  InvalidLightValue,      // light colour outside [0, 1] or negative/non-finite intensity
  TooManyLights,          // more than 1 directional or 4 point lights (Spec 0019 cap)
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
  // Milestone 4 material slice (Plan 0037 Rulings 2/3/4/7).
  std::uint32_t materialCount = 0;
  std::uint32_t materialsSpecGlossFormula = 0;          // D3 Khronos conversion
  std::uint32_t materialsSpecGlossTextureFallback = 0;  // Ruling 2 dielectric fallback
  std::uint32_t materialsMetallicRoughness = 0;         // core glTF model, passed through
  std::uint32_t materialsTransmission = 0;              // subset of the above, reported only
  std::uint32_t materialsWhiteFallback = 0;             // Ruling 7
  std::uint32_t texturesReferenced = 0;
  std::uint32_t colorSpaceWarnings = 0;  // Ruling 4
  // Milestone 5 scene slice (ADR-0083 D5/D6).
  std::uint32_t sceneNodeLines = 0;
  std::uint32_t sceneMeshLines = 0;
  std::uint32_t sceneLightLines = 0;
  std::uint32_t syntheticNodes = 0;  // extra primitives, or a light beside a mesh
  std::uint32_t sceneMaxDepth = 0;   // root = 1
  std::uint32_t meshesInstancedMoreThanOnce = 0;
  std::uint32_t nonUniformScaleNodes = 0;
  std::uint32_t primitivesWithoutMaterial = 0;
  std::uint32_t camerasDropped = 0;
  std::vector<std::string> reportLines;  // import_report.txt body
};

// inputPath: the .gltf file. contentRoot: directory against which relative
// buffer URIs resolve (Plan's --content-root; Bistro's bistro.bin sits next
// to the .gltf; texture URIs resolve against it too). All output is written to a sibling staging directory and
// renamed to outputDir only after every file is written, so a failed import
// leaves no output behind. outputDir must be absent or empty.
//
// Output layout: <name>_mesh_<i>_<j>.amesh(.meta.txt) per primitive,
// <name>/materials/<i>.material.txt per material, the Ruling 7 white
// fallback texture when needed, <name>/<name>.scene.txt for the default
// scene, import_report.txt, and cook_manifest.txt -- the
// atlantis_asset_cooker invocations (textures, then materials, then the
// scene) that turn the generated sources into artifacts.
[[nodiscard]] atlantis::Result<GltfImportSummary, GltfImportError> importGltf(
    const std::filesystem::path& inputPath, const std::filesystem::path& contentRoot,
    const std::filesystem::path& outputDir, const std::string& name);

[[nodiscard]] const char* gltfImportErrorMessage(GltfImportError error) noexcept;

}  // namespace atlantis::gltf_importer

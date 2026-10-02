#include "cook_command.h"
#include "dds_parser.h"
#include "guid_mint.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_set_validation.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_environment.h>
#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/environment_artifact.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/material_source.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/texture_types.h>

#include <array>
#include <charconv>
#include <filesystem>
#include <functional>
#include <random>
#include <set>

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Plan 0016 Section D9, extended by Plan 0025 Milestone 2: the one and only
// stb_image implementation translation unit in this target. It owns both the
// existing stbi_load() PNG path and the new stbi_loadf() Radiance HDR path -- the second
// implementation-macro translation unit in the repository (ADR-0041's
// own "one implementation-macro TU per linking target" Accepted
// Amendment; the first is tests/image_regression/support/png_codec.cpp),
// never linked into the same binary as that one. STB_IMAGE_WRITE_IMPLEMENTATION
// is deliberately NOT defined here -- this tool only ever decodes,
// mirroring cook_command.cpp's own read-only relationship to every other
// authoring source format it already handles.
#define STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
// stb's own implementation is not warning-clean under this project's
// /W4 /WX policy -- third-party code this project does not own or
// modify, matching png_codec.cpp's own identical, already-established
// suppression scope exactly (narrowly around this one include, not a
// relaxation of atlantis_compiler_warnings itself).
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace atlantis::tools::asset_cooker {

namespace {

namespace fs = std::filesystem;
using namespace atlantis::asset_system;

constexpr std::string_view kAuthoringExtension = ".mesh.txt";
// Plan 0015 Section D7: mirrors kAuthoringExtension exactly, for the
// scene pipeline's own source-relative-path/output-basename
// computation.
constexpr std::string_view kSceneAuthoringExtension = ".scene.txt";
// Plan 0018 Section P4: mirrors kAuthoringExtension exactly, for the
// material pipeline's own source-relative-path/output-basename
// computation.
constexpr std::string_view kMaterialAuthoringExtension = ".material.txt";
constexpr std::string_view kEnvironmentAuthoringExtension = ".hdr";

// Computes the source path relative to the asset root, as a forward-
// slash string, purely for CLI convenience (constructing an output file
// name and a default logical-path input). This is never the authority
// on whether the resulting string is a valid logical path --
// cookStaticMesh() (via normalizeLogicalPath(), Plan 0012 Section D9)
// is the sole authority on that, and independently re-validates
// whatever this function returns.
[[nodiscard]] std::string computeRelativePathString(const std::string& sourcePath, const std::string& assetRoot) {
  std::error_code ec;
  const fs::path relative = fs::relative(fs::path(sourcePath), fs::path(assetRoot), ec);
  if (ec) return sourcePath;
  return relative.generic_string();
}

[[nodiscard]] const char* catalogSourceParseErrorMessage(atlantis::asset_system::CatalogSourceParseError error);

// Plan 0047 P7: the GUID a per-asset cook writes. On the command line it
// comes from the catalog source, looked up as assets:<logical path>; on a
// cook-manifest line the importer supplies it as --guid=. Reports and
// returns nullopt on SourceNotInCatalog, CatalogTypeMismatch, an unreadable
// or invalid catalog source, or a malformed --guid=.
[[nodiscard]] std::optional<atlantis::asset_system::AssetGuid> resolveCookGuid(
    const CookCommandRequest& request, atlantis::asset_system::CatalogAssetType expectedType) {
  using atlantis::asset_system::CatalogRoot;

  if (!request.guid.empty()) {
    const auto guid = atlantis::asset_system::parseAssetGuid(request.guid);
    if (guid.isErr()) {
      std::cerr << "atlantis_asset_cooker: --guid is not a canonical, non-nil GUID: " << request.guid << "\n";
      return std::nullopt;
    }
    return guid.value();
  }

  std::ifstream file(request.catalogSourcePath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "atlantis_asset_cooker: cannot open catalog source: " << request.catalogSourcePath << "\n";
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const auto catalog = atlantis::asset_system::parseAssetCatalogSource(text);
  if (catalog.isErr()) {
    std::cerr << "atlantis_asset_cooker: invalid catalog source " << request.catalogSourcePath << ": "
              << catalogSourceParseErrorMessage(catalog.error()) << "\n";
    return std::nullopt;
  }

  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const auto logicalPath = normalizeLogicalPath(relativePath);
  const atlantis::asset_system::CatalogSourceEntry* entry =
      logicalPath.isOk() ? catalog.value().find(CatalogRoot::Assets, logicalPath.value()) : nullptr;
  if (entry == nullptr) {
    std::cerr << "atlantis_asset_cooker: SourceNotInCatalog: assets:" << relativePath << " has no entry in "
              << request.catalogSourcePath << "\n";
    return std::nullopt;
  }
  if (entry->type != expectedType) {
    std::cerr << "atlantis_asset_cooker: CatalogTypeMismatch: assets:" << relativePath << " is cataloged as "
              << atlantis::asset_system::toString(entry->type) << ", cooked as "
              << atlantis::asset_system::toString(expectedType) << "\n";
    return std::nullopt;
  }
  return entry->guid;
}

[[nodiscard]] std::string stripAuthoringExtension(const std::string& relativePath, std::string_view extension) {
  if (relativePath.size() > extension.size() &&
      relativePath.compare(relativePath.size() - extension.size(), extension.size(), extension) == 0) {
    return relativePath.substr(0, relativePath.size() - extension.size());
  }
  return relativePath;
}

[[nodiscard]] const char* cookErrorMessage(CookError error) {
  switch (error) {
    case CookError::SourceFileUnreadable:
      return "source file unreadable";
    case CookError::SourceParseFailed:
      return "source parse failed";
    case CookError::LogicalPathInvalid:
      return "logical path invalid";
    case CookError::ArtifactWriteFailed:
      return "artifact write failed";
    case CookError::MetadataWriteFailed:
      return "metadata write failed";
    case CookError::DegenerateTangentBasis:
      return "degenerate tangent basis";
    case CookError::TangentHandednessConflict:
      return "tangent handedness conflict";
  }
  return "unknown cook error";
}

[[nodiscard]] const char* assetSetErrorMessage(AssetSetError error) {
  switch (error) {
    case AssetSetError::AssetIdCollision:
      return "asset ID collision";
    case AssetSetError::CaseOnlyPathConflict:
      return "case-only logical path conflict";
    case AssetSetError::DuplicateLogicalPath:
      return "duplicate logical path";
    case AssetSetError::InvalidLogicalPath:
      return "invalid (not already normalized) logical path";
  }
  return "unknown asset set error";
}

// Plan 0016 Section D9: mirrors cookErrorMessage()'s own role and shape
// exactly, for TextureCookError.
[[nodiscard]] const char* textureCookErrorMessage(TextureCookError error) {
  switch (error) {
    case TextureCookError::ZeroDimension:
      return "zero width or height";
    case TextureCookError::DimensionExceedsMaximum:
      return "dimension exceeds maximum";
    case TextureCookError::SourceOverflow:
      return "pixel data size overflow";
    case TextureCookError::LogicalPathInvalid:
      return "logical path invalid";
    case TextureCookError::AtomicWriteFailed:
      return "atomic write failed";
    case TextureCookError::NonAlignedDimensions:
      return "BC7 base-mip width/height not a multiple of 4";
    case TextureCookError::BlockDataSizeMismatch:
      return "BC7 block byte count does not match the mip chain's byte count";
    case TextureCookError::InvalidMipCount:
      return "mip count is 0 or exceeds the dimensions' full mip chain";
  }
  return "unknown texture cook error";
}

// Plan 0015 Section D7: mirrors cookErrorMessage()'s own role and
// shape exactly, for SceneCookError.
[[nodiscard]] const char* sceneCookErrorMessage(SceneCookError error) {
  switch (error) {
    case SceneCookError::SourceFileUnreadable:
      return "source file unreadable";
    case SceneCookError::SourceParseFailed:
      return "source parse failed";
    case SceneCookError::EmptyScene:
      return "scene has no nodes";
    case SceneCookError::DuplicateNodeId:
      return "duplicate node_id";
    case SceneCookError::UndeclaredParentReference:
      return "parent references an undeclared node_id";
    case SceneCookError::ParentCycle:
      return "parent chain contains a cycle";
    case SceneCookError::UndeclaredActiveCameraReference:
      return "active_camera references an undeclared node_id";
    case SceneCookError::ActiveCameraMissingCamera:
      return "active_camera node has no camera fields";
    case SceneCookError::NonFiniteValue:
      return "non-finite authored value";
    case SceneCookError::NilEntityGuid:
      return "a node's guid= is nil";
    case SceneCookError::DuplicateEntityGuid:
      return "two nodes share one guid=";
    case SceneCookError::ArtifactWriteFailed:
      return "artifact write failed";
    case SceneCookError::MetadataWriteFailed:
      return "metadata write failed";
  }
  return "unknown scene cook error";
}

// The stamp is a disposable completion marker, not valuable data --
// written last, after both real files are already atomically in place
// (Plan 0012 Section D4), but its own write does not need D10's
// temp-then-rename treatment: if this write fails or is interrupted,
// CMake simply sees a missing/stale stamp and re-cooks next time,
// which is always safe. Shared by both cook modes (mesh and scene) --
// unlike the file-format helpers this codebase otherwise duplicates
// per translation unit, this is the same file cooperating with itself,
// not a cross-module boundary.
[[nodiscard]] bool writeStamp(const std::string& stampPath) {
  if (stampPath.empty()) return true;
  std::ofstream stamp(stampPath, std::ios::binary | std::ios::trunc);
  if (!stamp.is_open()) return false;
  stamp << "ok\n";
  return true;
}

// Temp-then-rename, so a reader never sees a half-written file.
[[nodiscard]] bool writeFileReplacing(const fs::path& path, const std::string& text) {
  const fs::path temp = path.string() + ".tmp";
  {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    if (!out.good()) return false;
  }
  std::error_code ec;
  fs::rename(temp, path, ec);
  return !ec;
}

[[nodiscard]] std::optional<std::string> readTextFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad()) return std::nullopt;
  return text;
}

// Plan 0047 P12's source_schema table: the source grammar version a cook
// reads. Textures, environments and imported meshes have none.
constexpr std::uint32_t kMeshSourceSchema = 3;
constexpr std::uint32_t kSceneSourceSchema = 7;
constexpr std::uint32_t kMaterialSourceSchema = 10;

// The mesh format is the one with two schemas (u16 and u32 indices), so a
// mesh record reads its schema back from the artifact the cook wrote.
[[nodiscard]] std::optional<std::uint32_t> meshArtifactSchema(const fs::path& artifactPath) {
  const auto text = readTextFile(artifactPath);
  if (!text) return std::nullopt;
  const std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(text->data()),
                                     reinterpret_cast<const std::byte*>(text->data()) + text->size());
  const auto header = peekMeshArtifactHeader(bytes);
  if (header.isErr()) return std::nullopt;
  return header.value().schemaVersion;
}

[[nodiscard]] std::string absoluteLocation(const fs::path& path) {
  return fs::absolute(path).lexically_normal().generic_string();
}

// Plan 0047 P12 / ADR-0098 D2: the one-record fragment every cook writes
// beside its artifact, <artifact>.catalog.txt, with absolute locations.
// dependencies are the GUIDs the cooked source references.
[[nodiscard]] bool writeCookFragment(const CookCommandRequest& request, CatalogAssetType type, const AssetGuid& guid,
                                     const fs::path& artifactPath, const fs::path& metadataPath,
                                     std::uint32_t artifactSchema, std::optional<std::uint32_t> sourceSchema,
                                     std::vector<AssetGuid> dependencies) {
  AssetCatalogRecord record;
  record.guid = guid;
  record.assetId = assetKey(guid);
  record.type = type;
  if (!request.catalogId.empty()) {
    const auto id = parseCatalogSourceId(request.catalogId);
    if (!id) {
      std::cerr << "atlantis_asset_cooker: --catalog-id is not <root>:<path>[#<sub-key>]: " << request.catalogId
                << "\n";
      return false;
    }
    record.source = *id;
  } else {
    const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
    const auto logicalPath = normalizeLogicalPath(relativePath);
    if (logicalPath.isErr()) {
      std::cerr << "atlantis_asset_cooker: invalid logical path for the catalog fragment: " << relativePath << "\n";
      return false;
    }
    record.source = CatalogSourceId{CatalogRoot::Assets, logicalPath.value(), ""};
  }
  record.artifact = absoluteLocation(artifactPath);
  record.metadata = absoluteLocation(metadataPath);
  record.artifactSchema = artifactSchema;
  record.sourceSchema = sourceSchema;
  record.tool = std::string(kImporterVersion);
  record.dependencies = std::move(dependencies);
  const fs::path fragmentPath = artifactPath.string() + ".catalog.txt";
  if (!writeFileReplacing(fragmentPath, serializeAssetCatalog({record}))) {
    std::cerr << "atlantis_asset_cooker: failed to write catalog fragment: " << fragmentPath.string() << "\n";
    return false;
  }
  return true;
}

// The mesh and material GUIDs a scene source references.
[[nodiscard]] std::optional<std::vector<AssetGuid>> sceneDependencies(const std::string& sourcePath) {
  const auto text = readTextFile(sourcePath);
  if (!text) return std::nullopt;
  const auto parsed = parseSceneSource(*text);
  if (parsed.isErr()) return std::nullopt;
  std::vector<AssetGuid> dependencies;
  for (const ParsedSceneNode& node : parsed.value().nodes) {
    if (node.meshAsset) dependencies.push_back(*node.meshAsset);
    if (node.materialAsset) dependencies.push_back(*node.materialAsset);
  }
  return dependencies;
}

// The texture GUIDs a material source references.
[[nodiscard]] std::optional<std::vector<AssetGuid>> materialDependencies(const std::string& sourcePath) {
  const auto text = readTextFile(sourcePath);
  if (!text) return std::nullopt;
  const auto parsed = parseMaterialSource(*text);
  if (parsed.isErr()) return std::nullopt;
  std::vector<AssetGuid> dependencies{parsed.value().textureAsset};
  if (parsed.value().normalMapAsset) dependencies.push_back(*parsed.value().normalMapAsset);
  if (parsed.value().emissiveTextureAsset) dependencies.push_back(*parsed.value().emissiveTextureAsset);
  return dependencies;
}

// Where a per-asset cook writes its artifact -- the same names each cook
// mode computes.
[[nodiscard]] fs::path cookArtifactPath(const CookCommandRequest& request) {
  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const fs::path outputDir(request.outputDir);
  switch (request.kind) {
    case AssetKind::StaticMesh:
      return outputDir / (stripAuthoringExtension(relativePath, kAuthoringExtension) + ".amesh");
    case AssetKind::Scene:
      return outputDir / (stripAuthoringExtension(relativePath, kSceneAuthoringExtension) + ".ascene");
    case AssetKind::Material:
      return outputDir / (stripAuthoringExtension(relativePath, kMaterialAuthoringExtension) + ".amaterial");
    case AssetKind::Texture:
      return outputDir / (fs::path(request.stampPath).stem().string() + ".atex");
    case AssetKind::Environment:
      return outputDir / (fs::path(request.stampPath).stem().string() + ".aenv");
    case AssetKind::CookManifest:
    case AssetKind::MintGuid:
    case AssetKind::Lookup:
    case AssetKind::Migrate0047:
    case AssetKind::AssembleCatalog:
      break;
  }
  return {};
}

[[nodiscard]] int runCookMeshMode(const CookCommandRequest& request) {
  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const std::string base = stripAuthoringExtension(relativePath, kAuthoringExtension);

  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".amesh");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".amesh.meta.txt");

  const auto guid = resolveCookGuid(request, atlantis::asset_system::CatalogAssetType::Mesh);
  if (!guid) return 1;
  const auto result =
      cookStaticMesh(request.sourcePath, relativePath, *guid, artifactPath.string(), metadataPath.string());
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << cookErrorMessage(result.error()) << "\n";
    return 1;
  }
  const auto meshSchema = meshArtifactSchema(artifactPath);
  if (!meshSchema) {
    std::cerr << "atlantis_asset_cooker: cannot read the cooked mesh's schema: " << artifactPath.string() << "\n";
    return 1;
  }
  if (!writeCookFragment(request, CatalogAssetType::Mesh, *guid, artifactPath, metadataPath, *meshSchema,
                         kMeshSourceSchema, {})) {
    return 1;
  }

  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }

  return 0;
}

// Plan 0015 Section D7: mirrors runCookMeshMode()'s own shape exactly.
// No logicalPathInput parameter -- cookScene() takes none (a scene has
// no AssetId of its own, D2); relativePath is only needed here to
// compute the output basename, not passed into cookScene() at all.
[[nodiscard]] int runCookSceneMode(const CookCommandRequest& request) {
  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const std::string base = stripAuthoringExtension(relativePath, kSceneAuthoringExtension);

  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".ascene");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".ascene.meta.txt");

  const auto guid = resolveCookGuid(request, atlantis::asset_system::CatalogAssetType::Scene);
  if (!guid) return 1;
  const auto result = cookScene(request.sourcePath, *guid, artifactPath.string(), metadataPath.string());
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << sceneCookErrorMessage(result.error()) << "\n";
    return 1;
  }
  auto dependencies = sceneDependencies(request.sourcePath);
  if (!dependencies) {
    std::cerr << "atlantis_asset_cooker: cannot re-read the cooked scene source: " << request.sourcePath << "\n";
    return 1;
  }
  if (!writeCookFragment(request, CatalogAssetType::Scene, *guid, artifactPath, metadataPath,
                         kSceneArtifactSchemaVersion, kSceneSourceSchema, std::move(*dependencies))) {
    return 1;
  }

  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }

  return 0;
}

// Plan 0016 Section D9: matches runCookMeshMode()/runCookSceneMode()'s
// own overall shape (decode -> cook -> stamp), with one disclosed
// deviation from D9's own literal "strip a fixed authoring extension"
// output-path description. Unlike mesh/scene, the same source PNG must
// be cook-able TWICE, under two different NAMEs/color spaces (Plan 0016
// Section D8's own explicit "NAME, not SOURCE, is the per-artifact
// identity key" requirement -- the mechanism the textured fixture's own
// two textures, Milestone 9, need) -- a source-relative-path-derived
// output basename cannot support that without one cook silently
// overwriting the other's artifact. The output basename is therefore
// derived from --stamp='s own filename stem instead, which
// atlantis_add_texture_asset() (asset_system/CMakeLists.txt, already
// committed in Milestone 6) already keys by NAME (<name>.stamp) to
// exactly match this function's own <name>.atex/<name>.atex.meta.txt
// expectation -- no CMake change was needed to make this line up.
// relativePath (the source's own asset-root-relative path) is still
// used for cookTexture()'s own logicalPathInput, unchanged from every
// other cook mode's own convention.
[[nodiscard]] const char* ddsParseErrorMessage(atlantis::asset_cooker::DdsParseError error) {
  using atlantis::asset_cooker::DdsParseError;
  switch (error) {
    case DdsParseError::MalformedHeader:
      return "malformed DDS header";
    case DdsParseError::UnsupportedFormat:
      return "DDS file is not a 2D BC7 texture (DXGI 99/100, or legacy fourCC 'BC7')";
    case DdsParseError::Truncated:
      return "DDS file truncated before its claimed base-mip bytes";
    case DdsParseError::NonAlignedDimensions:
      return "BC7 base-mip width/height not a multiple of 4";
  }
  return "unknown DDS parse error";
}

// Spec 0038/Plan 0038 Milestone 2: the BC7 cook path -- a .dds source is
// parsed (header only, blocks passed through verbatim, never decoded) and
// funneled into cookTextureBc7(). The DDS file's own DXGI format decides
// the color space, with one exception (Spec 0046 Q8, ruled 2026-09-26):
// --color-space=srgb cooks a BC7_UNORM (DXGI 99) file as sRGB -- the
// importer's flag for a texture used as colour; the blocks are identical,
// only the view format changes. Any other value, or none, leaves the file's
// own tag in force.
[[nodiscard]] int runCookTextureDdsMode(const CookCommandRequest& request) {
  using atlantis::asset_system::cookTextureBc7;

  std::ifstream file(request.sourcePath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "atlantis_asset_cooker: failed to open DDS source: " << request.sourcePath << "\n";
    return 1;
  }
  std::vector<std::uint8_t> ddsBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad()) {
    std::cerr << "atlantis_asset_cooker: failed to read DDS source: " << request.sourcePath << "\n";
    return 1;
  }

  const auto parsed = atlantis::asset_cooker::parseDdsBc7(ddsBytes.data(), ddsBytes.size());
  if (parsed.isErr()) {
    std::cerr << "atlantis_asset_cooker: " << ddsParseErrorMessage(parsed.error())
              << ": " << request.sourcePath << "\n";
    return 1;
  }
  const atlantis::asset_cooker::DdsBc7Image& image = parsed.value();

  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const std::string base = fs::path(request.stampPath).stem().string();
  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".atex");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".atex.meta.txt");

  // Spec 0045: the whole chain the DDS carries, passed through verbatim.
  const bool srgb = image.srgb || request.colorSpace == "srgb";
  const auto guid = resolveCookGuid(request, atlantis::asset_system::CatalogAssetType::Texture);
  if (!guid) return 1;
  const auto result = cookTextureBc7(image.blockBytes.data(), image.blockBytes.size(), image.width, image.height,
                                     image.mipCount,
                                     srgb ? atlantis::asset_system::TextureColorSpace::Srgb
                                          : atlantis::asset_system::TextureColorSpace::Unorm,
                                     relativePath, *guid, artifactPath, metadataPath);
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << textureCookErrorMessage(result.error()) << "\n";
    return 1;
  }
  if (!writeCookFragment(request, CatalogAssetType::Texture, *guid, artifactPath, metadataPath,
                         kTextureArtifactSchemaVersion, std::nullopt, {})) {
    return 1;
  }

  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }

  return 0;
}

[[nodiscard]] int runCookTextureMode(const CookCommandRequest& request) {
  using atlantis::asset_system::cookTexture;
  using atlantis::asset_system::TextureColorSpace;

  if (fs::path(request.sourcePath).extension() == ".dds") {
    return runCookTextureDdsMode(request);
  }

  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);

  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  if (request.colorSpace == "srgb") {
    colorSpace = TextureColorSpace::Srgb;
  } else if (request.colorSpace == "unorm") {
    colorSpace = TextureColorSpace::Unorm;
  } else {
    std::cerr << "atlantis_asset_cooker: unrecognized --color-space value: " << request.colorSpace << "\n";
    return 1;
  }

  // The one and only stbi_load() call site in this entire codebase's
  // own runtime-adjacent code (this file's own top comment). Requests 4
  // channels unconditionally -- channelsInFile reports the source's own
  // real decoded channel count for metadata provenance only (Spec 0016
  // Human Review item 9); a non-RGBA source is never rejected here,
  // deliberately unlike png_codec.cpp's own golden-validation
  // ChannelCountMismatch/UnsupportedBitDepth checks.
  int width = 0;
  int height = 0;
  int channelsInFile = 0;
  unsigned char* decoded = stbi_load(request.sourcePath.c_str(), &width, &height, &channelsInFile, 4);
  if (decoded == nullptr) {
    std::cerr << "atlantis_asset_cooker: failed to decode PNG source: " << request.sourcePath << "\n";
    return 1;
  }

  const std::string base = fs::path(request.stampPath).stem().string();
  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".atex");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".atex.meta.txt");

  const auto guid = resolveCookGuid(request, atlantis::asset_system::CatalogAssetType::Texture);
  if (!guid) return 1;
  const auto result = cookTexture(decoded, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                   channelsInFile, colorSpace, relativePath, *guid, artifactPath, metadataPath);
  stbi_image_free(decoded);
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << textureCookErrorMessage(result.error()) << "\n";
    return 1;
  }
  if (!writeCookFragment(request, CatalogAssetType::Texture, *guid, artifactPath, metadataPath,
                         kTextureArtifactSchemaVersion, std::nullopt, {})) {
    return 1;
  }

  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }

  return 0;
}

// Plan 0018 Section P4: mirrors runCookMeshMode()'s own shape exactly
// -- Material, unlike Scene, IS its own asset type with its own AssetId
// (Spec 0018 D1), so it takes a logicalPathInput exactly like
// cookStaticMesh()/cookTexture() do.
[[nodiscard]] const char* materialCookErrorMessage(atlantis::asset_system::MaterialCookError error) {
  switch (error) {
    case atlantis::asset_system::MaterialCookError::SourceFileUnreadable:
      return "source file unreadable";
    case atlantis::asset_system::MaterialCookError::SourceParseFailed:
      return "source parse failed";
    case atlantis::asset_system::MaterialCookError::LogicalPathInvalid:
      return "logical path invalid";
    case atlantis::asset_system::MaterialCookError::AtomicWriteFailed:
      return "atomic write failed";
    case atlantis::asset_system::MaterialCookError::BaseColorFactorOutOfRange:
      return "base_color_factor component out of range (must be finite, in [0, 1])";
    case atlantis::asset_system::MaterialCookError::MaterialFactorOutOfRange:
      return "metallic_factor/roughness_factor/clearcoat_factor/clearcoat_roughness/sheen_color/sheen_roughness "
             "out of range (must be finite, in [0, 1]), or anisotropy_factor out of range (must be finite, in "
             "[-1, 1]), or anisotropy_rotation not finite";
    case atlantis::asset_system::MaterialCookError::EmissiveFactorOutOfRange:
      return "emissive_factor component out of range (must be finite, in [0, 65504])";
  }
  return "unknown material cook error";
}

[[nodiscard]] const char* environmentCookErrorMessage(EnvironmentCookError error) {
  switch (error) {
    case EnvironmentCookError::LogicalPathInvalid:
      return "logical path invalid";
    case EnvironmentCookError::InvalidSourceDimensions:
      return "HDR source must be a non-zero 2:1 equirectangular image";
    case EnvironmentCookError::SourceSizeOverflow:
      return "HDR source size overflow";
    case EnvironmentCookError::NonFiniteSourceValue:
      return "HDR source contains a non-finite RGB value";
    case EnvironmentCookError::NegativeSourceValue:
      return "HDR source contains a negative RGB value";
    case EnvironmentCookError::OutputValueOverflow:
      return "processed environment exceeds binary16 range";
    case EnvironmentCookError::AtomicWriteFailed:
      return "atomic write failed";
  }
  return "unknown environment cook error";
}

[[nodiscard]] int runCookEnvironmentMode(const CookCommandRequest& request) {
  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  if (relativePath.size() <= kEnvironmentAuthoringExtension.size() ||
      !relativePath.ends_with(kEnvironmentAuthoringExtension)) {
    std::cerr << "atlantis_asset_cooker: environment source must use the .hdr extension: " << request.sourcePath
              << "\n";
    return 1;
  }
  int width = 0;
  int height = 0;
  int channelsInFile = 0;
  float* decoded = stbi_loadf(request.sourcePath.c_str(), &width, &height, &channelsInFile, 4);
  if (decoded == nullptr) {
    std::cerr << "atlantis_asset_cooker: failed to decode Radiance HDR source: " << request.sourcePath << "\n";
    return 1;
  }

  const std::string base = fs::path(request.stampPath).stem().string();
  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".aenv");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".aenv.meta.txt");
  const auto guid = resolveCookGuid(request, atlantis::asset_system::CatalogAssetType::Environment);
  if (!guid) return 1;
  const auto result = cookEnvironment(decoded, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                      relativePath, *guid, artifactPath, metadataPath);
  stbi_image_free(decoded);
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << environmentCookErrorMessage(result.error()) << "\n";
    return 1;
  }
  if (!writeCookFragment(request, CatalogAssetType::Environment, *guid, artifactPath, metadataPath,
                         kEnvironmentArtifactSchemaVersion, std::nullopt, {})) {
    return 1;
  }
  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }
  return 0;
}

[[nodiscard]] int runCookMaterialMode(const CookCommandRequest& request) {
  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const std::string base = stripAuthoringExtension(relativePath, kMaterialAuthoringExtension);

  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".amaterial");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".amaterial.meta.txt");

  const auto guid = resolveCookGuid(request, atlantis::asset_system::CatalogAssetType::Material);
  if (!guid) return 1;
  const auto result =
      cookMaterial(request.sourcePath, relativePath, *guid, artifactPath.string(), metadataPath.string());
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << materialCookErrorMessage(result.error()) << "\n";
    return 1;
  }
  auto dependencies = materialDependencies(request.sourcePath);
  if (!dependencies) {
    std::cerr << "atlantis_asset_cooker: cannot re-read the cooked material source: " << request.sourcePath << "\n";
    return 1;
  }
  if (!writeCookFragment(request, CatalogAssetType::Material, *guid, artifactPath, metadataPath,
                         kMaterialArtifactSchemaVersion, kMaterialSourceSchema, std::move(*dependencies))) {
    return 1;
  }

  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }

  return 0;
}

[[nodiscard]] int runValidateSetMode(const CookCommandRequest& request) {
  std::ifstream listFile(request.assetListPath);
  if (!listFile.is_open()) {
    std::cerr << "atlantis_asset_cooker: cannot open asset list: " << request.assetListPath << "\n";
    return 1;
  }

  std::vector<DeclaredAsset> assets;
  std::string line;
  while (std::getline(listFile, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    const auto normalizedResult = normalizeLogicalPath(line);
    if (normalizedResult.isErr()) {
      std::cerr << "atlantis_asset_cooker: invalid declared logical path: " << line << "\n";
      return 1;
    }
    const std::string& normalizedPath = normalizedResult.value();
    assets.push_back(DeclaredAsset{normalizedPath, computeAssetId(normalizedPath)});
  }

  const auto validationResult = validateAssetSet(assets);
  if (validationResult.isErr()) {
    std::cerr << "atlantis_asset_cooker: asset set validation failed: "
               << assetSetErrorMessage(validationResult.error()) << "\n";
    return 1;
  }

  return 0;
}

// Plan 0046 Milestone 2 (ADR-0094 Decision 2, Plan 0046 P8): the
// cook-manifest mode. Validates the import's asset list, runs every line of
// its cook_manifest.txt through the same per-kind modes a command line
// would (in-process, no std::system), then writes the Runtime dependency
// manifest -- one "logicalPath\tartifactPath\tmetadataPath" line per asset
// of the asset list, in its order: meshes at their artifacts in the import
// directory (found through each .amesh.meta.txt's own source_logical_path),
// textures and materials at their cooked artifacts. The scene is cooked
// but, having no AssetId, is not a manifest entry. Any failing line fails
// the whole mode.
[[nodiscard]] std::string substitutePlaceholders(std::string token, const CookCommandRequest& request) {
  const std::pair<std::string_view, const std::string*> placeholders[] = {
      {"{content_parent}", &request.contentParent},
      {"{import_dir}", &request.importDir},
      {"{cooked_dir}", &request.cookedDir},
  };
  for (const auto& [placeholder, value] : placeholders) {
    for (std::size_t at = token.find(placeholder); at != std::string::npos; at = token.find(placeholder, at)) {
      token.replace(at, placeholder.size(), *value);
      at += value->size();
    }
  }
  return token;
}

[[nodiscard]] std::optional<std::string> normalizedOrNull(const std::string& path) {
  const auto normalized = normalizeLogicalPath(path);
  if (normalized.isErr()) return std::nullopt;
  return normalized.value();
}

struct ManifestEntry {
  std::string artifactPath;
  std::string metadataPath;
};

[[nodiscard]] int runCookManifestMode(const CookCommandRequest& request) {
  const fs::path importDir(request.importDir);
  const fs::path cookedDir(request.cookedDir);

  CookCommandRequest validate;
  validate.isValidateSet = true;
  validate.assetListPath = (importDir / "asset_list.txt").string();
  if (runValidateSetMode(validate) != 0) return 1;

  std::ifstream manifestFile(importDir / "cook_manifest.txt");
  if (!manifestFile.is_open()) {
    std::cerr << "atlantis_asset_cooker: cannot open cook manifest: " << (importDir / "cook_manifest.txt").string()
              << "\n";
    return 1;
  }
  std::map<std::string, ManifestEntry> entries;  // normalized logical path -> artifact/metadata
  std::vector<fs::path> fragmentPaths;
  std::string line;
  std::size_t lineNumber = 0;
  std::size_t cooked = 0;
  while (std::getline(manifestFile, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    // Split before substituting, so a substituted path may hold spaces.
    std::vector<std::string> args;
    std::istringstream tokens(line);
    for (std::string token; tokens >> token;) args.push_back(substitutePlaceholders(token, request));
    CookCommandRequest lineRequest;
    if (!parseCookArguments(args, lineRequest, std::cerr, CookArgumentSource::CookManifestLine) ||
        lineRequest.isValidateSet ||
        lineRequest.kind == AssetKind::CookManifest || lineRequest.kind == AssetKind::Environment ||
        lineRequest.kind == AssetKind::MintGuid || lineRequest.kind == AssetKind::Lookup ||
        lineRequest.kind == AssetKind::Migrate0047 || lineRequest.kind == AssetKind::AssembleCatalog) {
      std::cerr << "atlantis_asset_cooker: cook manifest line " << lineNumber << " is not a texture/material/"
                << "scene/mesh cook: " << line << "\n";
      return 1;
    }
    if (runCookCommand(lineRequest) != 0) {
      std::cerr << "atlantis_asset_cooker: cook manifest line " << lineNumber << " failed: " << line << "\n";
      return 1;
    }
    ++cooked;
    fragmentPaths.push_back(cookArtifactPath(lineRequest).string() + ".catalog.txt");

    const std::string relativePath = computeRelativePathString(lineRequest.sourcePath, lineRequest.assetRoot);
    const auto logical = normalizedOrNull(relativePath);
    if (!logical) continue;
    const fs::path outputDir(lineRequest.outputDir);
    if (lineRequest.kind == AssetKind::Texture) {
      const std::string base = fs::path(lineRequest.stampPath).stem().string();
      entries[*logical] = {(outputDir / (base + ".atex")).generic_string(),
                           (outputDir / (base + ".atex.meta.txt")).generic_string()};
    } else if (lineRequest.kind == AssetKind::Material) {
      const std::string base = stripAuthoringExtension(relativePath, kMaterialAuthoringExtension);
      entries[*logical] = {(outputDir / (base + ".amaterial")).generic_string(),
                           (outputDir / (base + ".amaterial.meta.txt")).generic_string()};
    } else if (lineRequest.kind == AssetKind::StaticMesh) {
      const std::string base = stripAuthoringExtension(relativePath, kAuthoringExtension);
      entries[*logical] = {(outputDir / (base + ".amesh")).generic_string(),
                           (outputDir / (base + ".amesh.meta.txt")).generic_string()};
    }
  }

  // Meshes are written by the importer itself, never cooked: each artifact's
  // metadata names its own logical path.
  std::error_code ec;
  std::vector<fs::path> meshFragmentPaths;
  for (const auto& file : fs::directory_iterator(importDir, ec)) {
    const std::string filename = file.path().filename().string();
    if (filename.ends_with(".amesh.catalog.txt")) meshFragmentPaths.push_back(file.path());
    if (!filename.ends_with(".amesh.meta.txt")) continue;
    std::ifstream in(file.path(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto metadata = parseAssetMetadata(text);
    if (metadata.isErr()) {
      std::cerr << "atlantis_asset_cooker: unreadable mesh metadata: " << file.path().string() << "\n";
      return 1;
    }
    const auto logical = normalizedOrNull(metadata.value().sourceLogicalPath);
    if (!logical) continue;
    const std::string stem = filename.substr(0, filename.size() - std::string_view(".meta.txt").size());
    entries[*logical] = {(importDir / stem).generic_string(), file.path().generic_string()};
  }
  if (ec) {
    std::cerr << "atlantis_asset_cooker: cannot list import directory: " << importDir.string() << "\n";
    return 1;
  }

  // Plan 0047 P12: every record of this import -- each cook's fragment and
  // the importer's mesh fragments, their {import_dir} resolved -- merged
  // into <import dir>/import.catalog.txt, the import's one assembly input.
  const std::string importDirLocation = absoluteLocation(importDir);
  std::vector<AssetCatalogRecord> importRecords;
  const auto addFragment = [&importRecords, &importDirLocation](const fs::path& path, bool resolveImportDir) {
    auto text = readTextFile(path);
    if (!text) {
      std::cerr << "atlantis_asset_cooker: cannot read catalog fragment: " << path.string() << "\n";
      return false;
    }
    if (resolveImportDir) {
      constexpr std::string_view kPlaceholder = "{import_dir}";
      for (std::size_t at = text->find(kPlaceholder); at != std::string::npos; at = text->find(kPlaceholder, at)) {
        text->replace(at, kPlaceholder.size(), importDirLocation);
        at += importDirLocation.size();
      }
    }
    auto records = parseAssetCatalogRecords(*text);
    if (records.isErr()) {
      std::cerr << "atlantis_asset_cooker: malformed catalog fragment: " << path.string() << "\n";
      return false;
    }
    for (AssetCatalogRecord& record : records.value()) importRecords.push_back(std::move(record));
    return true;
  };
  for (const fs::path& path : fragmentPaths) {
    if (!addFragment(path, false)) return 1;
  }
  for (const fs::path& path : meshFragmentPaths) {
    if (!addFragment(path, true)) return 1;
  }
  const std::size_t importRecordCount = importRecords.size();
  const fs::path importCatalogPath = importDir / "import.catalog.txt";
  if (!writeFileReplacing(importCatalogPath, serializeAssetCatalog(std::move(importRecords)))) {
    std::cerr << "atlantis_asset_cooker: failed to write import catalog fragment: " << importCatalogPath.string()
              << "\n";
    return 1;
  }

  std::ifstream listFile(validate.assetListPath);
  std::string manifestText;
  std::size_t listed = 0;
  while (std::getline(listFile, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto logical = normalizedOrNull(line);
    const auto entry = logical ? entries.find(*logical) : entries.end();
    if (entry == entries.end()) {
      std::cerr << "atlantis_asset_cooker: declared asset has no cooked or imported artifact: " << line << "\n";
      return 1;
    }
    manifestText += line + "\t" + entry->second.artifactPath + "\t" + entry->second.metadataPath + "\n";
    ++listed;
  }
  // Temp-then-rename, so a consumer never reads a half-written manifest.
  const fs::path manifestOut(request.manifestOutPath);
  const fs::path manifestTemp = manifestOut.string() + ".tmp";
  bool manifestWritten = false;
  {
    std::error_code dirEc;
    fs::create_directories(manifestOut.parent_path(), dirEc);
    std::ofstream out(manifestTemp, std::ios::binary | std::ios::trunc);
    out << manifestText;
    out.flush();
    manifestWritten = out.good();
  }
  if (manifestWritten) {
    std::error_code renameEc;
    fs::rename(manifestTemp, manifestOut, renameEc);
    manifestWritten = !renameEc;
  }
  if (!manifestWritten) {
    std::cerr << "atlantis_asset_cooker: failed to write dependency manifest: " << request.manifestOutPath << "\n";
    return 1;
  }
  std::cout << "atlantis_asset_cooker: cook manifest: " << cooked << " cooks, " << listed
            << " dependency-manifest entries -> " << request.manifestOutPath << ", " << importRecordCount
            << " catalog records -> " << importCatalogPath.generic_string() << "\n";
  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }
  return 0;
}

// Plan 0047 P4: one canonical-text GUID per line on stdout.
[[nodiscard]] int runMintGuidMode(const CookCommandRequest& request) {
  for (const atlantis::asset_system::AssetGuid& guid : mintAssetGuids(request.mintCount)) {
    std::cout << atlantis::asset_system::toString(guid) << "\n";
  }
  return 0;
}

[[nodiscard]] const char* catalogSourceParseErrorMessage(atlantis::asset_system::CatalogSourceParseError error) {
  using atlantis::asset_system::CatalogSourceParseError;
  switch (error) {
    case CatalogSourceParseError::UnknownVersion:
      return "unknown catalog source version";
    case CatalogSourceParseError::EntryCountMismatch:
      return "entry_count does not match the entries";
    case CatalogSourceParseError::MalformedEntry:
      return "malformed entry";
    case CatalogSourceParseError::NilGuid:
      return "nil GUID";
    case CatalogSourceParseError::UnknownType:
      return "unknown asset type";
    case CatalogSourceParseError::UnknownRoot:
      return "unknown root";
    case CatalogSourceParseError::NonNormalPath:
      return "path is not in normalized logical-path form";
    case CatalogSourceParseError::Unsorted:
      return "entries are not in (root, path) order";
    case CatalogSourceParseError::DuplicateGuid:
      return "duplicate GUID";
    case CatalogSourceParseError::DuplicatePath:
      return "duplicate (root, path)";
  }
  return "unknown error";
}

// Plan 0047 M2: prints the matching catalog-source entry line, the
// readability aid Spec 0047 Q4 relies on.
// Plan 0047 M4: the matching record of a cooked catalog, by --guid= or by
// its full source (<root>:<path>[#<sub-key>]).
[[nodiscard]] int runLookupCatalogMode(const CookCommandRequest& request) {
  const auto text = readTextFile(request.catalogPath);
  if (!text) {
    std::cerr << "atlantis_asset_cooker: cannot open catalog: " << request.catalogPath << "\n";
    return 1;
  }
  const auto records = parseAssetCatalogRecords(*text);
  if (records.isErr()) {
    std::cerr << "atlantis_asset_cooker: invalid catalog: " << request.catalogPath << "\n";
    return 1;
  }
  std::optional<AssetGuid> guid;
  if (!request.guid.empty()) {
    const auto parsed = parseAssetGuid(request.guid);
    if (parsed.isErr()) {
      std::cerr << "atlantis_asset_cooker: --guid is not a canonical GUID: " << request.guid << "\n";
      return 1;
    }
    guid = parsed.value();
  }
  for (const AssetCatalogRecord& record : records.value()) {
    if (guid ? record.guid == *guid : toString(record.source) == request.sourcePath) {
      std::cout << formatAssetCatalogRecord(record) << "\n";
      return 0;
    }
  }
  std::cerr << "atlantis_asset_cooker: no catalog record for "
            << (request.guid.empty() ? request.sourcePath : request.guid) << "\n";
  return 1;
}

[[nodiscard]] int runLookupMode(const CookCommandRequest& request) {
  using atlantis::asset_system::CatalogRoot;
  using atlantis::asset_system::CatalogSourceEntry;

  if (!request.catalogPath.empty()) return runLookupCatalogMode(request);

  std::ifstream file(request.catalogSourcePath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "atlantis_asset_cooker: cannot open catalog source: " << request.catalogSourcePath << "\n";
    return 1;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const auto catalog = atlantis::asset_system::parseAssetCatalogSource(text);
  if (catalog.isErr()) {
    std::cerr << "atlantis_asset_cooker: invalid catalog source " << request.catalogSourcePath << ": "
              << catalogSourceParseErrorMessage(catalog.error()) << "\n";
    return 1;
  }

  const CatalogSourceEntry* found = nullptr;
  if (!request.guid.empty()) {
    const auto guid = atlantis::asset_system::parseAssetGuid(request.guid);
    if (guid.isErr()) {
      std::cerr << "atlantis_asset_cooker: --guid is not a canonical GUID: " << request.guid << "\n";
      return 1;
    }
    found = catalog.value().find(guid.value());
  } else {
    const std::size_t colon = request.sourcePath.find(':');
    const auto root = colon == std::string::npos
                          ? std::nullopt
                          : atlantis::asset_system::parseCatalogRoot(std::string_view(request.sourcePath).substr(0, colon));
    if (!root) {
      std::cerr << "atlantis_asset_cooker: --source must be <assets|content>:<path>: " << request.sourcePath << "\n";
      return 1;
    }
    found = catalog.value().find(*root, std::string_view(request.sourcePath).substr(colon + 1));
  }
  if (found == nullptr) {
    std::cerr << "atlantis_asset_cooker: no catalog source entry for "
              << (request.guid.empty() ? request.sourcePath : request.guid) << "\n";
    return 1;
  }
  std::cout << atlantis::asset_system::formatCatalogSourceEntry(*found) << "\n";
  return 0;
}

// Plan 0047 P20 (one-off; removed in M9): mints the catalog source from the
// declarations list and rewrites every declared scene (v6 -> v7) and
// material (v9 -> v10) source, plus the undeclared scene sources named by
// --scene-source= (ruling I5), on text tokens. Every rewrite is computed in
// memory first; any unmappable reference or unexpected version fails the
// run before a single file is written.
namespace migrate {

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::CatalogAssetType;
using atlantis::asset_system::CatalogRoot;
using atlantis::asset_system::CatalogSourceEntry;

constexpr std::string_view kSceneV6 = "atlantis_scene_source_version: 6";
constexpr std::string_view kSceneV7 = "atlantis_scene_source_version: 7";
constexpr std::string_view kSceneVersionPrefix = "atlantis_scene_source_version:";
constexpr std::string_view kMaterialV9 = "atlantis_material_source_version: 9";
constexpr std::string_view kMaterialV10 = "atlantis_material_source_version: 10";
constexpr std::string_view kMaterialVersionPrefix = "atlantis_material_source_version:";
constexpr std::array<std::string_view, 3> kTextureReferencePrefixes = {"texture: ", "normal_map: ",
                                                                        "emissive_texture: "};

struct Line {
  std::string_view content;
  std::string_view terminator;  // "\r\n", "\n", or empty on a final unterminated line
};

[[nodiscard]] std::vector<Line> splitKeepingTerminators(std::string_view text) {
  std::vector<Line> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t newline = text.find('\n', start);
    if (newline == std::string_view::npos) {
      lines.push_back({text.substr(start), {}});
      break;
    }
    std::size_t contentEnd = newline;
    if (contentEnd > start && text[contentEnd - 1] == '\r') --contentEnd;
    lines.push_back({text.substr(start, contentEnd - start), text.substr(contentEnd, newline + 1 - contentEnd)});
    start = newline + 1;
  }
  return lines;
}

class GuidMinter {
 public:
  GuidMinter() = default;
  GuidMinter(const GuidMinter&) = delete;
  GuidMinter& operator=(const GuidMinter&) = delete;

  [[nodiscard]] AssetGuid next() {
    for (;;) {
      const AssetGuid guid = mintAssetGuid(draw_);
      if (issued_.insert(guid).second) return guid;
    }
  }

 private:
  std::random_device device_;
  std::function<std::uint32_t()> draw_ = [this] { return static_cast<std::uint32_t>(device_()); };
  std::set<AssetGuid> issued_;
};

class Migration {
 public:
  explicit Migration(std::map<std::pair<CatalogRoot, std::string>, CatalogSourceEntry> entries)
      : entries_(std::move(entries)) {}

  // Returns the rewritten text, or nullopt after reporting the first
  // failure on stderr.
  [[nodiscard]] std::optional<std::string> rewriteScene(const std::string& label, std::string_view text,
                                                        GuidMinter& minter) const {
    std::string out;
    bool sawVersion = false;
    std::size_t lineNumber = 0;
    for (const Line& line : splitKeepingTerminators(text)) {
      ++lineNumber;
      const std::string where = label + ":" + std::to_string(lineNumber);
      if (line.content.substr(0, kSceneVersionPrefix.size()) == kSceneVersionPrefix) {
        if (line.content != kSceneV6 || sawVersion) return fail(where, "expected one '" + std::string(kSceneV6) + "'");
        sawVersion = true;
        out += kSceneV7;
      } else if (line.content.substr(0, 6) == "node: ") {
        auto rewritten = rewriteNodeLine(where, line.content, minter);
        if (!rewritten) return std::nullopt;
        out += *rewritten;
      } else {
        out += line.content;
      }
      out += line.terminator;
    }
    if (!sawVersion) return fail(label, "no scene source version line");
    return out;
  }

  [[nodiscard]] std::optional<std::string> rewriteMaterial(const std::string& label, std::string_view text) const {
    std::string out;
    bool sawVersion = false;
    std::size_t lineNumber = 0;
    for (const Line& line : splitKeepingTerminators(text)) {
      ++lineNumber;
      const std::string where = label + ":" + std::to_string(lineNumber);
      std::string_view content = line.content;
      if (content.substr(0, kMaterialVersionPrefix.size()) == kMaterialVersionPrefix) {
        if (content != kMaterialV9 || sawVersion) {
          return fail(where, "expected one '" + std::string(kMaterialV9) + "'");
        }
        sawVersion = true;
        out += kMaterialV10;
      } else if (const auto prefix = textureReferencePrefix(content)) {
        const auto guid = resolve(where, content.substr(prefix->size()), CatalogAssetType::Texture);
        if (!guid) return std::nullopt;
        out += *prefix;
        out += atlantis::asset_system::toString(*guid);
      } else {
        out += content;
      }
      out += line.terminator;
    }
    if (!sawVersion) return fail(label, "no material source version line");
    return out;
  }

 private:
  [[nodiscard]] static std::nullopt_t fail(const std::string& where, const std::string& what) {
    std::cerr << "atlantis_asset_cooker: migrate-0047: " << where << ": " << what << "\n";
    return std::nullopt;
  }

  [[nodiscard]] static std::optional<std::string_view> textureReferencePrefix(std::string_view content) {
    for (std::string_view prefix : kTextureReferencePrefixes) {
      if (content.substr(0, prefix.size()) == prefix) return prefix;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<AssetGuid> resolve(const std::string& where, std::string_view reference,
                                                 CatalogAssetType expected) const {
    const auto normalized = normalizeLogicalPath(reference);
    if (normalized.isErr()) return fail(where, "reference is not a logical path: " + std::string(reference));
    const auto it = entries_.find({CatalogRoot::Assets, normalized.value()});
    if (it == entries_.end()) return fail(where, "reference is not a declared asset: " + std::string(reference));
    if (it->second.type != expected) {
      return fail(where, "reference names a " + std::string(atlantis::asset_system::toString(it->second.type)) +
                             ", expected a " + std::string(atlantis::asset_system::toString(expected)) + ": " +
                             std::string(reference));
    }
    return it->second.guid;
  }

  // Inserts " guid=<minted>" after the node_id token and replaces the
  // mesh=/material= values, leaving every other byte of the line as it was.
  [[nodiscard]] std::optional<std::string> rewriteNodeLine(const std::string& where, std::string_view content,
                                                           GuidMinter& minter) const {
    std::string out;
    bool sawNodeId = false;
    std::size_t copied = 0;
    std::size_t pos = 0;
    while (pos < content.size()) {
      if (content[pos] == ' ') {
        ++pos;
        continue;
      }
      std::size_t end = content.find(' ', pos);
      if (end == std::string_view::npos) end = content.size();
      const std::string_view token = content.substr(pos, end - pos);
      std::optional<CatalogAssetType> referenceType;
      std::size_t valueOffset = 0;
      if (token.substr(0, 5) == "mesh=") {
        referenceType = CatalogAssetType::Mesh;
        valueOffset = 5;
      } else if (token.substr(0, 9) == "material=") {
        referenceType = CatalogAssetType::Material;
        valueOffset = 9;
      }
      if (referenceType) {
        const auto guid = resolve(where, token.substr(valueOffset), *referenceType);
        if (!guid) return std::nullopt;
        out += content.substr(copied, pos + valueOffset - copied);
        out += atlantis::asset_system::toString(*guid);
        copied = end;
      } else if (token.substr(0, 8) == "node_id=" && !sawNodeId) {
        sawNodeId = true;
        out += content.substr(copied, end - copied);
        out += " guid=";
        out += atlantis::asset_system::toString(minter.next());
        copied = end;
      }
      pos = end;
    }
    if (!sawNodeId) return fail(where, "node line has no node_id token");
    out += content.substr(copied);
    return out;
  }

  std::map<std::pair<CatalogRoot, std::string>, CatalogSourceEntry> entries_;
};

[[nodiscard]] std::optional<std::string> readText(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

}  // namespace migrate

[[nodiscard]] int runMigrate0047Mode(const CookCommandRequest& request) {
  using atlantis::asset_system::CatalogAssetType;
  using atlantis::asset_system::CatalogRoot;
  using atlantis::asset_system::CatalogSourceEntry;

  if (fs::exists(request.catalogSourceOutPath)) {
    std::cerr << "atlantis_asset_cooker: migrate-0047: refusing to overwrite " << request.catalogSourceOutPath
              << "\n";
    return 1;
  }
  const auto declarationsText = migrate::readText(request.declarationsPath);
  if (!declarationsText) {
    std::cerr << "atlantis_asset_cooker: migrate-0047: cannot read " << request.declarationsPath << "\n";
    return 1;
  }

  migrate::GuidMinter minter;
  std::map<std::pair<CatalogRoot, std::string>, CatalogSourceEntry> entries;
  std::vector<std::string> scenePaths;
  std::vector<std::string> materialPaths;
  std::size_t lineNumber = 0;
  for (const migrate::Line& line : migrate::splitKeepingTerminators(*declarationsText)) {
    ++lineNumber;
    if (line.content.empty()) continue;
    const std::string where = request.declarationsPath + ":" + std::to_string(lineNumber);
    const std::size_t first = line.content.find('\t');
    const std::size_t second = first == std::string_view::npos ? first : line.content.find('\t', first + 1);
    const auto type = first == std::string_view::npos ? std::nullopt
                                                     : atlantis::asset_system::parseCatalogAssetType(
                                                           line.content.substr(0, first));
    const auto root = second == std::string_view::npos
                          ? std::nullopt
                          : atlantis::asset_system::parseCatalogRoot(line.content.substr(first + 1, second - first - 1));
    if (!type || !root) {
      std::cerr << "atlantis_asset_cooker: migrate-0047: " << where << ": malformed declaration\n";
      return 1;
    }
    const std::string path(line.content.substr(second + 1));
    CatalogSourceEntry entry{minter.next(), *type, *root, path};
    if (!entries.emplace(std::pair{*root, path}, entry).second) {
      std::cerr << "atlantis_asset_cooker: migrate-0047: " << where << ": duplicate declaration " << path << "\n";
      return 1;
    }
    if (*root == CatalogRoot::Assets && *type == CatalogAssetType::Scene) scenePaths.push_back(path);
    if (*root == CatalogRoot::Assets && *type == CatalogAssetType::Material) materialPaths.push_back(path);
  }
  for (const std::string& extra : request.extraSceneSources) {
    if (entries.contains({CatalogRoot::Assets, extra})) {
      std::cerr << "atlantis_asset_cooker: migrate-0047: --scene-source names a declared asset: " << extra << "\n";
      return 1;
    }
    scenePaths.push_back(extra);
  }

  std::vector<CatalogSourceEntry> catalogEntries;
  for (const auto& [key, entry] : entries) catalogEntries.push_back(entry);
  const std::string catalogText = atlantis::asset_system::serializeAssetCatalogSource(catalogEntries);
  if (atlantis::asset_system::parseAssetCatalogSource(catalogText).isErr()) {
    std::cerr << "atlantis_asset_cooker: migrate-0047: the declarations do not form a valid catalog source\n";
    return 1;
  }

  const migrate::Migration migration(std::move(entries));
  std::vector<std::pair<fs::path, std::string>> rewrites;
  const fs::path assetRoot(request.assetRoot);
  const auto rewriteAll = [&](const std::vector<std::string>& paths, bool scene) {
    for (const std::string& path : paths) {
      const auto text = migrate::readText(assetRoot / path);
      if (!text) {
        std::cerr << "atlantis_asset_cooker: migrate-0047: cannot read " << (assetRoot / path).string() << "\n";
        return false;
      }
      const auto rewritten =
          scene ? migration.rewriteScene(path, *text, minter) : migration.rewriteMaterial(path, *text);
      if (!rewritten) return false;
      rewrites.emplace_back(assetRoot / path, *rewritten);
    }
    return true;
  };
  if (!rewriteAll(scenePaths, true) || !rewriteAll(materialPaths, false)) return 1;

  const auto writeAll = [](const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
  };
  if (!writeAll(request.catalogSourceOutPath, catalogText)) {
    std::cerr << "atlantis_asset_cooker: migrate-0047: cannot write " << request.catalogSourceOutPath << "\n";
    return 1;
  }
  for (const auto& [path, text] : rewrites) {
    if (!writeAll(path, text)) {
      std::cerr << "atlantis_asset_cooker: migrate-0047: cannot write " << path.string() << "\n";
      return 1;
    }
  }
  std::cout << "atlantis_asset_cooker: migrate-0047: " << catalogEntries.size() << " catalog entries, "
            << scenePaths.size() << " scene sources, " << materialPaths.size() << " material sources\n";
  return 0;
}

// Plan 0047 P13 (ADR-0098 D2): reads the catalog source, the declarations
// and the fragment list (one path per line), assembles, and writes the
// catalog and each closure only when the whole set passes.
[[nodiscard]] int runAssembleCatalogMode(const CookCommandRequest& request) {
  const auto sourceText = readTextFile(request.catalogSourcePath);
  if (!sourceText) {
    std::cerr << "atlantis_asset_cooker: cannot open catalog source: " << request.catalogSourcePath << "\n";
    return 1;
  }
  const auto catalogSource = parseAssetCatalogSource(*sourceText);
  if (catalogSource.isErr()) {
    std::cerr << "atlantis_asset_cooker: invalid catalog source " << request.catalogSourcePath << ": "
              << catalogSourceParseErrorMessage(catalogSource.error()) << "\n";
    return 1;
  }
  const auto declarationsText = readTextFile(request.declarationsPath);
  const auto declarations = declarationsText ? parseAssetDeclarations(*declarationsText) : std::nullopt;
  if (!declarations) {
    std::cerr << "atlantis_asset_cooker: cannot read declarations: " << request.declarationsPath << "\n";
    return 1;
  }
  const auto fragmentListText = readTextFile(request.fragmentListPath);
  if (!fragmentListText) {
    std::cerr << "atlantis_asset_cooker: cannot read fragment list: " << request.fragmentListPath << "\n";
    return 1;
  }

  AssetCatalogAssemblyRequest assembly;
  assembly.catalogSource = &catalogSource.value();
  assembly.declarations = *declarations;
  std::istringstream fragmentLines(*fragmentListText);
  for (std::string line; std::getline(fragmentLines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) assembly.fragmentPaths.push_back(line);
  }
  assembly.outPath = request.outPath;
  for (const auto& [sceneText, outPath] : request.closures) {
    const auto scene = parseAssetGuid(sceneText);
    if (scene.isErr()) {
      std::cerr << "atlantis_asset_cooker: --closure scene is not a canonical GUID: " << sceneText << "\n";
      return 1;
    }
    assembly.closures.push_back(AssetCatalogClosureRequest{scene.value(), outPath});
  }

  const auto assembled = assembleAssetCatalog(assembly);
  if (assembled.isErr()) {
    std::cerr << "atlantis_asset_cooker: assemble-catalog: " << toString(assembled.error().error) << ": "
              << assembled.error().subject << "\n";
    return 1;
  }
  if (!writeFileReplacing(request.outPath, assembled.value().catalogText)) {
    std::cerr << "atlantis_asset_cooker: failed to write catalog: " << request.outPath << "\n";
    return 1;
  }
  for (std::size_t i = 0; i < assembly.closures.size(); ++i) {
    if (!writeFileReplacing(assembly.closures[i].outPath, assembled.value().closureTexts[i])) {
      std::cerr << "atlantis_asset_cooker: failed to write closure catalog: " << assembly.closures[i].outPath << "\n";
      return 1;
    }
  }
  std::cout << "atlantis_asset_cooker: assemble-catalog: " << assembled.value().recordCount << " records, "
            << assembled.value().undeclaredSourceEntries
            << " catalog-source entries not declared in this build (content-gated) -> " << request.outPath << "\n";
  return 0;
}

}  // namespace

bool parseCookArguments(const std::vector<std::string>& args, CookCommandRequest& request, std::ostream& err,
                        CookArgumentSource source) {
  bool sawSource = false, sawAssetRoot = false, sawOutputDir = false, sawAssetList = false;
  bool sawUnrecognized = false;
  const auto valueAfterEquals = [](std::string_view arg, std::string_view flag) -> std::optional<std::string> {
    if (arg.substr(0, flag.size()) != flag) return std::nullopt;
    return std::string(arg.substr(flag.size()));
  };

  for (const std::string& argString : args) {
    const std::string_view arg = argString;
    if (arg == "--validate-set") {
      request.isValidateSet = true;
    } else if (auto sourcePath = valueAfterEquals(arg, "--source=")) {
      request.sourcePath = *sourcePath;
      sawSource = true;
    } else if (auto assetRoot = valueAfterEquals(arg, "--asset-root=")) {
      request.assetRoot = *assetRoot;
      sawAssetRoot = true;
    } else if (auto outputDir = valueAfterEquals(arg, "--output-dir=")) {
      request.outputDir = *outputDir;
      sawOutputDir = true;
    } else if (auto stamp = valueAfterEquals(arg, "--stamp=")) {
      request.stampPath = *stamp;
    } else if (auto assetList = valueAfterEquals(arg, "--asset-list=")) {
      request.assetListPath = *assetList;
      sawAssetList = true;
    } else if (auto importDir = valueAfterEquals(arg, "--import-dir=")) {
      request.importDir = *importDir;
    } else if (auto cookedDir = valueAfterEquals(arg, "--cooked-dir=")) {
      request.cookedDir = *cookedDir;
    } else if (auto contentParent = valueAfterEquals(arg, "--content-parent=")) {
      request.contentParent = *contentParent;
    } else if (auto manifestOut = valueAfterEquals(arg, "--manifest-out=")) {
      request.manifestOutPath = *manifestOut;
    } else if (auto kind = valueAfterEquals(arg, "--kind=")) {
      if (*kind == "mesh") {
        request.kind = AssetKind::StaticMesh;
      } else if (*kind == "scene") {
        request.kind = AssetKind::Scene;
      } else if (*kind == "texture") {
        request.kind = AssetKind::Texture;
      } else if (*kind == "material") {
        request.kind = AssetKind::Material;
      } else if (*kind == "environment") {
        request.kind = AssetKind::Environment;
      } else if (*kind == "cook-manifest") {
        request.kind = AssetKind::CookManifest;
      } else if (*kind == "mint-guid") {
        request.kind = AssetKind::MintGuid;
      } else if (*kind == "lookup") {
        request.kind = AssetKind::Lookup;
      } else if (*kind == "migrate-0047") {
        request.kind = AssetKind::Migrate0047;
      } else if (*kind == "assemble-catalog") {
        request.kind = AssetKind::AssembleCatalog;
      } else {
        err << "atlantis_asset_cooker: unrecognized --kind value: " << *kind << "\n";
        sawUnrecognized = true;
      }
    } else if (auto colorSpace = valueAfterEquals(arg, "--color-space=")) {
      request.colorSpace = *colorSpace;
    } else if (auto catalogSource = valueAfterEquals(arg, "--catalog-source=")) {
      request.catalogSourcePath = *catalogSource;
    } else if (auto declarations = valueAfterEquals(arg, "--declarations=")) {
      request.declarationsPath = *declarations;
    } else if (auto catalogSourceOut = valueAfterEquals(arg, "--catalog-source-out=")) {
      request.catalogSourceOutPath = *catalogSourceOut;
    } else if (auto sceneSource = valueAfterEquals(arg, "--scene-source=")) {
      request.extraSceneSources.push_back(*sceneSource);
    } else if (auto guid = valueAfterEquals(arg, "--guid=")) {
      request.guid = *guid;
    } else if (auto catalogId = valueAfterEquals(arg, "--catalog-id=")) {
      request.catalogId = *catalogId;
    } else if (auto catalogPath = valueAfterEquals(arg, "--catalog=")) {
      request.catalogPath = *catalogPath;
    } else if (auto fragmentList = valueAfterEquals(arg, "--fragment-list=")) {
      request.fragmentListPath = *fragmentList;
    } else if (auto outPath = valueAfterEquals(arg, "--out=")) {
      request.outPath = *outPath;
    } else if (auto closure = valueAfterEquals(arg, "--closure=")) {
      const std::size_t eq = closure->find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 == closure->size()) {
        err << "atlantis_asset_cooker: --closure must be <scene guid>=<out>: " << *closure << "\n";
        sawUnrecognized = true;
      } else {
        request.closures.emplace_back(closure->substr(0, eq), closure->substr(eq + 1));
      }
    } else if (auto count = valueAfterEquals(arg, "--count=")) {
      std::uint32_t parsedCount = 0;
      const char* end = count->data() + count->size();
      const auto [ptr, ec] = std::from_chars(count->data(), end, parsedCount);
      if (ec != std::errc{} || ptr != end || parsedCount == 0) {
        err << "atlantis_asset_cooker: --count must be a positive integer: " << *count << "\n";
        sawUnrecognized = true;
      } else {
        request.mintCount = parsedCount;
      }
    } else {
      err << "atlantis_asset_cooker: unrecognized argument: " << arg << "\n";
      sawUnrecognized = true;
    }
  }

  bool haveRequiredFlags = false;
  if (request.isValidateSet) {
    haveRequiredFlags = sawAssetList;
  } else if (request.kind == AssetKind::MintGuid) {
    haveRequiredFlags = true;
  } else if (request.kind == AssetKind::Migrate0047) {
    haveRequiredFlags = !request.declarationsPath.empty() && sawAssetRoot && !request.catalogSourceOutPath.empty();
  } else if (request.kind == AssetKind::Lookup) {
    haveRequiredFlags = (request.catalogSourcePath.empty() != request.catalogPath.empty()) &&
                        (request.guid.empty() != !sawSource);
  } else if (request.kind == AssetKind::AssembleCatalog) {
    haveRequiredFlags = !request.catalogSourcePath.empty() && !request.declarationsPath.empty() &&
                        !request.fragmentListPath.empty() && !request.outPath.empty();
  } else if (request.kind == AssetKind::CookManifest) {
    haveRequiredFlags = !request.importDir.empty() && !request.cookedDir.empty() && !request.contentParent.empty() &&
                        !request.manifestOutPath.empty();
  } else {
    haveRequiredFlags = sawSource && sawAssetRoot && sawOutputDir;
  }
  // Plan 0047 P7: a per-asset cook takes its GUID from --catalog-source= on
  // the command line and from --guid= on a cook-manifest line, never the
  // other way round, so a build script can never carry a GUID.
  const bool isCookKind = !request.isValidateSet &&
                          (request.kind == AssetKind::StaticMesh || request.kind == AssetKind::Scene ||
                           request.kind == AssetKind::Texture || request.kind == AssetKind::Material ||
                           request.kind == AssetKind::Environment);
  if (!request.guid.empty() && request.kind != AssetKind::Lookup &&
      !(isCookKind && source == CookArgumentSource::CookManifestLine)) {
    err << "atlantis_asset_cooker: --guid= is accepted only with --kind=lookup or on a cook-manifest line\n";
    return false;
  }
  if (isCookKind && source == CookArgumentSource::CommandLine && request.catalogSourcePath.empty()) {
    err << "atlantis_asset_cooker: a cook needs --catalog-source=<path>\n";
    return false;
  }
  if (isCookKind && source == CookArgumentSource::CookManifestLine &&
      (request.guid.empty() || !request.catalogSourcePath.empty())) {
    err << "atlantis_asset_cooker: a cook-manifest line needs --guid= and no --catalog-source=\n";
    return false;
  }
  // Plan 0047 P12: likewise the imported record's source.
  if (!request.catalogPath.empty() && request.kind != AssetKind::Lookup) {
    err << "atlantis_asset_cooker: --catalog= is accepted only with --kind=lookup\n";
    return false;
  }
  if (!request.catalogId.empty() && !(isCookKind && source == CookArgumentSource::CookManifestLine)) {
    err << "atlantis_asset_cooker: --catalog-id= is accepted only on a cook-manifest line\n";
    return false;
  }
  if (isCookKind && source == CookArgumentSource::CookManifestLine &&
      !parseCatalogSourceId(request.catalogId).has_value()) {
    err << "atlantis_asset_cooker: a cook-manifest line needs --catalog-id=<root>:<path>[#<sub-key>]\n";
    return false;
  }
  return !sawUnrecognized && haveRequiredFlags;
}

int runCookCommand(const CookCommandRequest& request) {
  if (request.isValidateSet) return runValidateSetMode(request);
  switch (request.kind) {
    case AssetKind::StaticMesh:
      return runCookMeshMode(request);
    case AssetKind::Scene:
      return runCookSceneMode(request);
    case AssetKind::Texture:
      return runCookTextureMode(request);
    case AssetKind::Material:
      return runCookMaterialMode(request);
    case AssetKind::Environment:
      return runCookEnvironmentMode(request);
    case AssetKind::CookManifest:
      return runCookManifestMode(request);
    case AssetKind::MintGuid:
      return runMintGuidMode(request);
    case AssetKind::Lookup:
      return runLookupMode(request);
    case AssetKind::Migrate0047:
      return runMigrate0047Mode(request);
    case AssetKind::AssembleCatalog:
      return runAssembleCatalogMode(request);
  }
  return runCookMeshMode(request);
}

}  // namespace atlantis::tools::asset_cooker

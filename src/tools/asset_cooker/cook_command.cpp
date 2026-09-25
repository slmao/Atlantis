#include "cook_command.h"
#include "dds_parser.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_set_validation.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_environment.h>
#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/texture_types.h>

#include <filesystem>

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

[[nodiscard]] int runCookMeshMode(const CookCommandRequest& request) {
  const std::string relativePath = computeRelativePathString(request.sourcePath, request.assetRoot);
  const std::string base = stripAuthoringExtension(relativePath, kAuthoringExtension);

  const fs::path artifactPath = fs::path(request.outputDir) / (base + ".amesh");
  const fs::path metadataPath = fs::path(request.outputDir) / (base + ".amesh.meta.txt");

  const auto result =
      cookStaticMesh(request.sourcePath, relativePath, artifactPath.string(), metadataPath.string());
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << cookErrorMessage(result.error()) << "\n";
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

  const auto result = cookScene(request.sourcePath, artifactPath.string(), metadataPath.string());
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << sceneCookErrorMessage(result.error()) << "\n";
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
// funneled into cookTextureBc7(). The DDS file's own DXGI format is the
// sole color-space authority here: --color-space is ignored for .dds
// sources (it exists for the stb-decoded PNG path below only).
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
  const auto result = cookTextureBc7(image.blockBytes.data(), image.blockBytes.size(), image.width, image.height,
                                     image.mipCount,
                                     image.srgb ? atlantis::asset_system::TextureColorSpace::Srgb
                                                : atlantis::asset_system::TextureColorSpace::Unorm,
                                     relativePath, artifactPath, metadataPath);
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << textureCookErrorMessage(result.error()) << "\n";
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

  const auto result = cookTexture(decoded, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                   channelsInFile, colorSpace, relativePath, artifactPath, metadataPath);
  stbi_image_free(decoded);
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << textureCookErrorMessage(result.error()) << "\n";
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
  const auto result = cookEnvironment(decoded, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                      relativePath, artifactPath, metadataPath);
  stbi_image_free(decoded);
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << environmentCookErrorMessage(result.error()) << "\n";
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

  const auto result = cookMaterial(request.sourcePath, relativePath, artifactPath.string(), metadataPath.string());
  if (result.isErr()) {
    std::cerr << "atlantis_asset_cooker: cook failed: " << materialCookErrorMessage(result.error()) << "\n";
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
    if (!parseCookArguments(args, lineRequest, std::cerr) || lineRequest.isValidateSet ||
        lineRequest.kind == AssetKind::CookManifest || lineRequest.kind == AssetKind::Environment) {
      std::cerr << "atlantis_asset_cooker: cook manifest line " << lineNumber << " is not a texture/material/"
                << "scene/mesh cook: " << line << "\n";
      return 1;
    }
    if (runCookCommand(lineRequest) != 0) {
      std::cerr << "atlantis_asset_cooker: cook manifest line " << lineNumber << " failed: " << line << "\n";
      return 1;
    }
    ++cooked;

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
  for (const auto& file : fs::directory_iterator(importDir, ec)) {
    const std::string filename = file.path().filename().string();
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
            << " dependency-manifest entries -> " << request.manifestOutPath << "\n";
  if (!writeStamp(request.stampPath)) {
    std::cerr << "atlantis_asset_cooker: failed to write stamp file: " << request.stampPath << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

bool parseCookArguments(const std::vector<std::string>& args, CookCommandRequest& request, std::ostream& err) {
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
    } else if (auto source = valueAfterEquals(arg, "--source=")) {
      request.sourcePath = *source;
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
      } else {
        err << "atlantis_asset_cooker: unrecognized --kind value: " << *kind << "\n";
        sawUnrecognized = true;
      }
    } else if (auto colorSpace = valueAfterEquals(arg, "--color-space=")) {
      request.colorSpace = *colorSpace;
    } else {
      err << "atlantis_asset_cooker: unrecognized argument: " << arg << "\n";
      sawUnrecognized = true;
    }
  }

  bool haveRequiredFlags = false;
  if (request.isValidateSet) {
    haveRequiredFlags = sawAssetList;
  } else if (request.kind == AssetKind::CookManifest) {
    haveRequiredFlags = !request.importDir.empty() && !request.cookedDir.empty() && !request.contentParent.empty() &&
                        !request.manifestOutPath.empty();
  } else {
    haveRequiredFlags = sawSource && sawAssetRoot && sawOutputDir;
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
  }
  return runCookMeshMode(request);
}

}  // namespace atlantis::tools::asset_cooker

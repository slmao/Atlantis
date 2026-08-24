#include "cook_command.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_set_validation.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/texture_types.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Plan 0016 Section D9: the one and only stb_image call site in this
// entire codebase's own runtime-adjacent code -- the second
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
// Plan 0016 Section D9: mirrors kAuthoringExtension exactly, for the
// texture pipeline's own source-relative-path computation (the output
// basename itself is NAME-derived, not SOURCE-derived -- see
// runCookTextureMode()'s own comment).
constexpr std::string_view kTextureAuthoringExtension = ".png";

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
[[nodiscard]] int runCookTextureMode(const CookCommandRequest& request) {
  using atlantis::asset_system::cookTexture;
  using atlantis::asset_system::TextureColorSpace;

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

}  // namespace

int runCookCommand(const CookCommandRequest& request) {
  if (request.isValidateSet) return runValidateSetMode(request);
  switch (request.kind) {
    case AssetKind::StaticMesh:
      return runCookMeshMode(request);
    case AssetKind::Scene:
      return runCookSceneMode(request);
    case AssetKind::Texture:
      return runCookTextureMode(request);
  }
  return runCookMeshMode(request);
}

}  // namespace atlantis::tools::asset_cooker

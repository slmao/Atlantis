#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/result.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace atlantis::asset_system {

// Plan 0016 Section D8: takes ALREADY-DECODED pixel bytes -- this
// function never calls stbi_load() itself, and this module never links
// Stb::Stb at all (the established module-boundary resolution: PNG
// decode lives only in the Tools cooker's own runCookTextureMode(),
// tools/asset_cooker). pixelBytes must point to exactly
// width * height * 4 tightly-packed, row-major RGBA8 bytes (caller
// precondition, matching the artifact's own on-disk contract) --
// channelsInFile is recorded into the metadata sidecar as provenance
// only (Spec 0016 Human Review item 9), never used to reinterpret
// pixelBytes itself, since the caller's own stb_image invocation already
// requested 4 channels regardless of the source file's real channel
// count.
//
// Deviation from cookStaticMesh()'s own established shape, disclosed
// here: this function does NOT call normalizeLogicalPath() on
// logicalPathInput -- TextureCookError's own finite, Plan-0016-specified
// vocabulary (ZeroDimension, DimensionExceedsMaximum, SourceOverflow,
// AtomicWriteFailed) has no path-validation case, unlike CookError's own
// LogicalPathInvalid. logicalPathInput is trusted to already be a
// normalized, asset-root-relative path -- exactly what
// runCookTextureMode()'s own relativePath computation already produces
// (tools/asset_cooker), matching runCookMeshMode()'s own relativePath
// computation that cookStaticMesh() happens to re-normalize anyway, not
// a case this function newly relies on being correct.
[[nodiscard]] atlantis::Result<std::monostate, TextureCookError> cookTexture(
    const std::uint8_t* pixelBytes, std::uint32_t width, std::uint32_t height, std::int32_t channelsInFile,
    TextureColorSpace colorSpace, const std::string& logicalPathInput,
    const std::filesystem::path& artifactOutputPath, const std::filesystem::path& metadataOutputPath);

}  // namespace atlantis::asset_system

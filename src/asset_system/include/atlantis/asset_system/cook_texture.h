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
// Plan 0016 Section D8, corrected by the Plan's own "Human Review
// Correction -- 2026-08-24": calls normalizeLogicalPath() on
// logicalPathInput exactly like cookStaticMesh() does, returning
// TextureCookError::LogicalPathInvalid on failure -- restoring parity
// with cookStaticMesh()'s own established shape (the original design
// trusted logicalPathInput to already be normalized; that trust is what
// let two different cookTexture() callers silently share one AssetId,
// the exact gap this correction closes). The normalized path, not the
// caller-supplied one, is what computeAssetId() and the metadata
// sidecar's own sourceLogicalPath both use from here on.
[[nodiscard]] atlantis::Result<std::monostate, TextureCookError> cookTexture(
    const std::uint8_t* pixelBytes, std::uint32_t width, std::uint32_t height, std::int32_t channelsInFile,
    TextureColorSpace colorSpace, const std::string& logicalPathInput,
    const std::filesystem::path& artifactOutputPath, const std::filesystem::path& metadataOutputPath);

}  // namespace atlantis::asset_system

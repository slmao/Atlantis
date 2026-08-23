#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0016 Section D8: the texture metadata sidecar's field semantics
// -- a new, dedicated shape (not a reuse of AssetMetadata, whose 8-line
// mesh-specific shape does not fit), matching SceneMetadata's own
// precedent of a dedicated shape when the existing one does not apply.
// Wire encoding: strict, anchored-prefix, versioned flat text, exactly
// 7 lines, matching AssetMetadata/SceneMetadata's own established
// grammar discipline -- this module's parser is new, independent code,
// never shared with either of theirs.
//
// channelsInFile is the source's own real decoded channel count --
// provenance only, never a hard validation gate the way a golden's
// channels_in_file == 4 is (Spec 0016 Human Review item 9's own
// explicit, disclosed difference from the golden-validation model): it
// is recorded and round-tripped, but loadTextureAsset() never rejects a
// load based on its value.
struct TextureMetadata {
  AssetId assetId = 0;
  std::string sourceLogicalPath;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TextureColorSpace format = TextureColorSpace::Unorm;
  std::int32_t channelsInFile = 0;
};

[[nodiscard]] atlantis::Result<TextureMetadata, MetadataParseError> parseTextureMetadata(std::string_view text);
[[nodiscard]] std::string serializeTextureMetadata(const TextureMetadata& metadata);

}  // namespace atlantis::asset_system

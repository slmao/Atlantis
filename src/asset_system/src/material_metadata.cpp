#include <atlantis/asset_system/material_metadata.h>

#include <charconv>
#include <cstddef>
#include <system_error>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_material_metadata_version: 1";
constexpr std::string_view kAssetIdPrefix = "asset_id: ";
constexpr std::string_view kSourceLogicalPathPrefix = "source_logical_path: ";
constexpr std::string_view kKindPrefix = "kind: ";
constexpr std::string_view kTextureAssetPrefix = "texture_asset: ";
constexpr std::size_t kExpectedLineCount = 5;

constexpr std::string_view kKindUnlitTextured = "unlit_textured";
// Plan 0019 Section P5/D11: MaterialKind widened to a second
// enumerator -- this sidecar's own "kind:" field must accept/emit both
// values, mirroring material_source.cpp's own identical widening
// exactly. A real, previously-undisclosed gap this Plan's own
// Milestone 5 missed: this metadata-sidecar serializer/parser is a
// SIBLING of material_source.cpp's grammar, not the same file, and
// still hardcoded a single-enumerator vocabulary (this file's own former
// comment, "MaterialKind is a closed, single-enumerator vocabulary this
// round (ADR-0059 D2/D15)", is now stale and removed below) -- found via
// a real loadMaterialAsset() failure (MetadataArtifactMismatch) while
// building this Plan's own lighting_demo_scene fixture, not by
// inspection alone.
constexpr std::string_view kKindLitTextured = "lit_textured";

// Duplicated from texture_metadata.cpp's own identical helpers rather
// than shared, matching that file's own file-local, not-exported
// precedent for exactly this class of helper.
[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text) {
  if (text.empty()) return {};

  std::string_view body = text;
  if (body.back() == '\n') body.remove_suffix(1);

  std::vector<std::string_view> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    if (i == body.size() || body[i] == '\n') {
      std::string_view line = body.substr(start, i - start);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      lines.push_back(line);
      start = i + 1;
    }
  }
  return lines;
}

// Strict: only lowercase hex digits accepted, matching toHexString()'s
// own canonical output.
[[nodiscard]] bool parseAssetIdHex(std::string_view token, AssetId& out) {
  if (token.size() != 16) return false;
  for (char c : token) {
    const bool isLowerHexDigit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!isLowerHexDigit) return false;
  }
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out, 16);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

[[nodiscard]] bool matchField(std::string_view line, std::string_view prefix, std::string_view& valueOut) {
  if (line.substr(0, prefix.size()) != prefix) return false;
  valueOut = line.substr(prefix.size());
  return true;
}

}  // namespace

atlantis::Result<MaterialMetadata, MetadataParseError> parseMaterialMetadata(std::string_view text) {
  using ResultT = atlantis::Result<MaterialMetadata, MetadataParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.size() != kExpectedLineCount) return ResultT::Err(MetadataParseError::WrongLineCount);

  if (lines[0] != kVersionLine) return ResultT::Err(MetadataParseError::UnknownMetadataVersion);

  MaterialMetadata metadata;
  std::string_view value;

  if (!matchField(lines[1], kAssetIdPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseAssetIdHex(value, metadata.assetId)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[2], kSourceLogicalPathPrefix, value)) {
    return ResultT::Err(MetadataParseError::FieldNameMismatch);
  }
  metadata.sourceLogicalPath = std::string(value);

  if (!matchField(lines[3], kKindPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (value == kKindUnlitTextured) {
    metadata.kind = MaterialKind::UnlitTextured;
  } else if (value == kKindLitTextured) {
    metadata.kind = MaterialKind::LitTextured;
  } else {
    return ResultT::Err(MetadataParseError::MalformedValue);
  }

  if (!matchField(lines[4], kTextureAssetPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseAssetIdHex(value, metadata.textureAsset)) return ResultT::Err(MetadataParseError::MalformedValue);

  return ResultT::Ok(std::move(metadata));
}

std::string serializeMaterialMetadata(const MaterialMetadata& metadata) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kAssetIdPrefix;
  out += toHexString(metadata.assetId);
  out += '\n';
  out += kSourceLogicalPathPrefix;
  out += metadata.sourceLogicalPath;
  out += '\n';
  out += kKindPrefix;
  out += (metadata.kind == MaterialKind::UnlitTextured ? kKindUnlitTextured : kKindLitTextured);
  out += '\n';
  out += kTextureAssetPrefix;
  out += toHexString(metadata.textureAsset);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

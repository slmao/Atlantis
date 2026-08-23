#include <atlantis/asset_system/texture_metadata.h>

#include <charconv>
#include <cstddef>
#include <system_error>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_texture_metadata_version: 1";
constexpr std::string_view kAssetIdPrefix = "asset_id: ";
constexpr std::string_view kSourceLogicalPathPrefix = "source_logical_path: ";
constexpr std::string_view kWidthPrefix = "width: ";
constexpr std::string_view kHeightPrefix = "height: ";
constexpr std::string_view kFormatPrefix = "format: ";
constexpr std::string_view kChannelsInFilePrefix = "channels_in_file: ";
constexpr std::size_t kExpectedLineCount = 7;

constexpr std::string_view kFormatUnorm = "unorm";
constexpr std::string_view kFormatSrgb = "srgb";

// Duplicated from asset_metadata.cpp/scene_metadata.cpp's own identical
// helpers rather than shared, matching those files' own already-
// established duplication precedent.
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

[[nodiscard]] bool parseUnsigned(std::string_view token, std::uint32_t& out) {
  if (token.empty()) return false;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

[[nodiscard]] bool parseSigned(std::string_view token, std::int32_t& out) {
  if (token.empty()) return false;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
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

atlantis::Result<TextureMetadata, MetadataParseError> parseTextureMetadata(std::string_view text) {
  using ResultT = atlantis::Result<TextureMetadata, MetadataParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.size() != kExpectedLineCount) return ResultT::Err(MetadataParseError::WrongLineCount);

  if (lines[0] != kVersionLine) return ResultT::Err(MetadataParseError::UnknownMetadataVersion);

  TextureMetadata metadata;
  std::string_view value;

  if (!matchField(lines[1], kAssetIdPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseAssetIdHex(value, metadata.assetId)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[2], kSourceLogicalPathPrefix, value)) {
    return ResultT::Err(MetadataParseError::FieldNameMismatch);
  }
  metadata.sourceLogicalPath = std::string(value);

  if (!matchField(lines[3], kWidthPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseUnsigned(value, metadata.width)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[4], kHeightPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseUnsigned(value, metadata.height)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[5], kFormatPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (value == kFormatUnorm) {
    metadata.format = TextureColorSpace::Unorm;
  } else if (value == kFormatSrgb) {
    metadata.format = TextureColorSpace::Srgb;
  } else {
    return ResultT::Err(MetadataParseError::MalformedValue);
  }

  if (!matchField(lines[6], kChannelsInFilePrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseSigned(value, metadata.channelsInFile)) return ResultT::Err(MetadataParseError::MalformedValue);

  return ResultT::Ok(std::move(metadata));
}

std::string serializeTextureMetadata(const TextureMetadata& metadata) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kAssetIdPrefix;
  out += toHexString(metadata.assetId);
  out += '\n';
  out += kSourceLogicalPathPrefix;
  out += metadata.sourceLogicalPath;
  out += '\n';
  out += kWidthPrefix;
  out += std::to_string(metadata.width);
  out += '\n';
  out += kHeightPrefix;
  out += std::to_string(metadata.height);
  out += '\n';
  out += kFormatPrefix;
  out += (metadata.format == TextureColorSpace::Srgb ? kFormatSrgb : kFormatUnorm);
  out += '\n';
  out += kChannelsInFilePrefix;
  out += std::to_string(metadata.channelsInFile);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

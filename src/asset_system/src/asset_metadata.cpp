#include <atlantis/asset_system/asset_metadata.h>

#include <charconv>
#include <cstddef>
#include <system_error>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_asset_metadata_version: 1";
constexpr std::string_view kAssetIdPrefix = "asset_id: ";
constexpr std::string_view kSourceLogicalPathPrefix = "source_logical_path: ";
constexpr std::string_view kImporterVersionPrefix = "importer_version: ";
constexpr std::string_view kAssetTypePrefix = "asset_type: ";
constexpr std::string_view kVertexCountPrefix = "vertex_count: ";
constexpr std::string_view kIndexCountPrefix = "index_count: ";
constexpr std::string_view kVertexStrideBytesPrefix = "vertex_stride_bytes: ";
constexpr std::size_t kExpectedLineCount = 8;

// Duplicated from mesh_source.cpp's own identical helper rather than
// shared -- matching this project's own small-helper-duplication
// precedent (e.g. tests/image_regression/fixture's own duplicated setup
// sequence) over introducing a private cross-file header neither
// consumer otherwise needs.
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

// Strict: only lowercase hex digits accepted, matching toHexString()'s
// own canonical output -- an uppercase-hex or otherwise malformed
// asset_id value is rejected, not silently tolerated.
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

atlantis::Result<AssetMetadata, MetadataParseError> parseAssetMetadata(std::string_view text) {
  using ResultT = atlantis::Result<AssetMetadata, MetadataParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.size() != kExpectedLineCount) return ResultT::Err(MetadataParseError::WrongLineCount);

  if (lines[0] != kVersionLine) return ResultT::Err(MetadataParseError::UnknownMetadataVersion);

  AssetMetadata metadata;
  std::string_view value;

  if (!matchField(lines[1], kAssetIdPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseAssetIdHex(value, metadata.assetId)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[2], kSourceLogicalPathPrefix, value)) {
    return ResultT::Err(MetadataParseError::FieldNameMismatch);
  }
  metadata.sourceLogicalPath = std::string(value);

  if (!matchField(lines[3], kImporterVersionPrefix, value)) {
    return ResultT::Err(MetadataParseError::FieldNameMismatch);
  }
  metadata.importerVersion = std::string(value);

  if (!matchField(lines[4], kAssetTypePrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  metadata.assetType = std::string(value);

  if (!matchField(lines[5], kVertexCountPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseUnsigned(value, metadata.vertexCount)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[6], kIndexCountPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseUnsigned(value, metadata.indexCount)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[7], kVertexStrideBytesPrefix, value)) {
    return ResultT::Err(MetadataParseError::FieldNameMismatch);
  }
  if (!parseUnsigned(value, metadata.vertexStrideBytes)) return ResultT::Err(MetadataParseError::MalformedValue);

  return ResultT::Ok(std::move(metadata));
}

std::string serializeAssetMetadata(const AssetMetadata& metadata) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kAssetIdPrefix;
  out += toHexString(metadata.assetId);
  out += '\n';
  out += kSourceLogicalPathPrefix;
  out += metadata.sourceLogicalPath;
  out += '\n';
  out += kImporterVersionPrefix;
  out += metadata.importerVersion;
  out += '\n';
  out += kAssetTypePrefix;
  out += metadata.assetType;
  out += '\n';
  out += kVertexCountPrefix;
  out += std::to_string(metadata.vertexCount);
  out += '\n';
  out += kIndexCountPrefix;
  out += std::to_string(metadata.indexCount);
  out += '\n';
  out += kVertexStrideBytesPrefix;
  out += std::to_string(metadata.vertexStrideBytes);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

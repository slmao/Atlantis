#include <atlantis/asset_system/environment_metadata.h>

#include <atlantis/asset_system/asset_id.h>

#include <charconv>
#include <cstddef>
#include <system_error>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_environment_metadata_version: 1";
constexpr std::string_view kAssetIdPrefix = "asset_id: ";
constexpr std::string_view kSourceLogicalPathPrefix = "source_logical_path: ";
constexpr std::string_view kFaceSizePrefix = "face_size: ";
constexpr std::string_view kMipCountPrefix = "mip_count: ";
constexpr std::string_view kDfgWidthPrefix = "dfg_width: ";
constexpr std::string_view kDfgHeightPrefix = "dfg_height: ";
constexpr std::size_t kExpectedLineCount = 7;

[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text) {
  if (text.empty()) return {};
  if (text.back() == '\n') text.remove_suffix(1);
  std::vector<std::string_view> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      std::string_view line = text.substr(start, i - start);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      lines.push_back(line);
      start = i + 1;
    }
  }
  return lines;
}

[[nodiscard]] bool matchField(std::string_view line, std::string_view prefix, std::string_view& value) {
  if (!line.starts_with(prefix)) return false;
  value = line.substr(prefix.size());
  return true;
}

[[nodiscard]] bool parseU32(std::string_view token, std::uint32_t& out) {
  if (token.empty()) return false;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

[[nodiscard]] bool parseAssetId(std::string_view token, AssetId& out) {
  if (token.size() != 16) return false;
  for (char c : token) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out, 16);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

}  // namespace

atlantis::Result<EnvironmentMetadata, MetadataParseError> parseEnvironmentMetadata(std::string_view text) {
  using ResultT = atlantis::Result<EnvironmentMetadata, MetadataParseError>;
  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.size() != kExpectedLineCount) return ResultT::Err(MetadataParseError::WrongLineCount);
  if (lines[0] != kVersionLine) return ResultT::Err(MetadataParseError::UnknownMetadataVersion);

  EnvironmentMetadata metadata;
  std::string_view value;
  if (!matchField(lines[1], kAssetIdPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseAssetId(value, metadata.assetId)) return ResultT::Err(MetadataParseError::MalformedValue);
  if (!matchField(lines[2], kSourceLogicalPathPrefix, value)) {
    return ResultT::Err(MetadataParseError::FieldNameMismatch);
  }
  metadata.sourceLogicalPath = std::string(value);
  if (!matchField(lines[3], kFaceSizePrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseU32(value, metadata.faceSize)) return ResultT::Err(MetadataParseError::MalformedValue);
  if (!matchField(lines[4], kMipCountPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseU32(value, metadata.mipCount)) return ResultT::Err(MetadataParseError::MalformedValue);
  if (!matchField(lines[5], kDfgWidthPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseU32(value, metadata.dfgWidth)) return ResultT::Err(MetadataParseError::MalformedValue);
  if (!matchField(lines[6], kDfgHeightPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseU32(value, metadata.dfgHeight)) return ResultT::Err(MetadataParseError::MalformedValue);
  return ResultT::Ok(std::move(metadata));
}

std::string serializeEnvironmentMetadata(const EnvironmentMetadata& metadata) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kAssetIdPrefix;
  out += toHexString(metadata.assetId);
  out += '\n';
  out += kSourceLogicalPathPrefix;
  out += metadata.sourceLogicalPath;
  out += '\n';
  out += kFaceSizePrefix;
  out += std::to_string(metadata.faceSize);
  out += '\n';
  out += kMipCountPrefix;
  out += std::to_string(metadata.mipCount);
  out += '\n';
  out += kDfgWidthPrefix;
  out += std::to_string(metadata.dfgWidth);
  out += '\n';
  out += kDfgHeightPrefix;
  out += std::to_string(metadata.dfgHeight);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

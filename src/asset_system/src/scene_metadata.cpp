#include <atlantis/asset_system/scene_metadata.h>

#include <charconv>
#include <cstddef>
#include <system_error>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_scene_metadata_version: 1";
constexpr std::string_view kSchemaVersionPrefix = "schema_version: ";
constexpr std::string_view kNodeCountPrefix = "node_count: ";
constexpr std::size_t kExpectedLineCount = 3;

// Duplicated from asset_metadata.cpp's own identical helpers rather
// than shared, matching that file's own already-established
// duplication precedent (itself duplicated from mesh_source.cpp).
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

[[nodiscard]] bool matchField(std::string_view line, std::string_view prefix, std::string_view& valueOut) {
  if (line.substr(0, prefix.size()) != prefix) return false;
  valueOut = line.substr(prefix.size());
  return true;
}

}  // namespace

atlantis::Result<SceneMetadata, MetadataParseError> parseSceneMetadata(std::string_view text) {
  using ResultT = atlantis::Result<SceneMetadata, MetadataParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  if (lines.size() != kExpectedLineCount) return ResultT::Err(MetadataParseError::WrongLineCount);

  if (lines[0] != kVersionLine) return ResultT::Err(MetadataParseError::UnknownMetadataVersion);

  SceneMetadata metadata;
  std::string_view value;

  if (!matchField(lines[1], kSchemaVersionPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseUnsigned(value, metadata.schemaVersion)) return ResultT::Err(MetadataParseError::MalformedValue);

  if (!matchField(lines[2], kNodeCountPrefix, value)) return ResultT::Err(MetadataParseError::FieldNameMismatch);
  if (!parseUnsigned(value, metadata.nodeCount)) return ResultT::Err(MetadataParseError::MalformedValue);

  return ResultT::Ok(std::move(metadata));
}

std::string serializeSceneMetadata(const SceneMetadata& metadata) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kSchemaVersionPrefix;
  out += std::to_string(metadata.schemaVersion);
  out += '\n';
  out += kNodeCountPrefix;
  out += std::to_string(metadata.nodeCount);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

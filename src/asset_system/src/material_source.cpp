#include <atlantis/asset_system/material_source.h>

#include <cstddef>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_material_source_version: 1";
constexpr std::string_view kKindPrefix = "kind: ";
constexpr std::string_view kTexturePrefix = "texture: ";
constexpr std::string_view kFilterPrefix = "filter: ";
constexpr std::string_view kAddressModePrefix = "address_mode: ";

constexpr std::string_view kKindUnlitTextured = "unlit_textured";
constexpr std::string_view kKindLitTextured = "lit_textured";
constexpr std::string_view kFilterNearest = "nearest";
constexpr std::string_view kFilterLinear = "linear";
constexpr std::string_view kAddressModeRepeat = "repeat";
constexpr std::string_view kAddressModeClampToEdge = "clamp_to_edge";

// Duplicated from scene_source.cpp's/mesh_source.cpp's own identical
// helpers rather than shared, matching those files' own established
// "duplicated, not shared" precedent for exactly this class of helper.
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

[[nodiscard]] bool matchField(std::string_view line, std::string_view prefix, std::string_view& valueOut) {
  if (line.substr(0, prefix.size()) != prefix) return false;
  valueOut = line.substr(prefix.size());
  return true;
}

}  // namespace

atlantis::Result<ParsedMaterialSource, MaterialSourceParseError> parseMaterialSource(std::string_view text) {
  using ResultT = atlantis::Result<ParsedMaterialSource, MaterialSourceParseError>;

  const std::vector<std::string_view> lines = splitLines(text);

  constexpr std::size_t kExpectedLineCount = 5;
  if (lines.size() < kExpectedLineCount) return ResultT::Err(MaterialSourceParseError::MissingField);
  if (lines.size() > kExpectedLineCount) return ResultT::Err(MaterialSourceParseError::TrailingContent);

  if (lines[0] != kVersionLine) return ResultT::Err(MaterialSourceParseError::UnknownSourceVersion);

  ParsedMaterialSource parsed;
  std::string_view value;

  if (!matchField(lines[1], kKindPrefix, value)) return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
  if (value == kKindUnlitTextured) {
    parsed.kind = MaterialKind::UnlitTextured;
  } else if (value == kKindLitTextured) {
    parsed.kind = MaterialKind::LitTextured;
  } else {
    return ResultT::Err(MaterialSourceParseError::UnknownKind);
  }

  if (!matchField(lines[2], kTexturePrefix, value)) return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
  if (value.empty()) return ResultT::Err(MaterialSourceParseError::MissingField);
  parsed.textureLogicalPath = std::string(value);

  if (!matchField(lines[3], kFilterPrefix, value)) return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
  if (value == kFilterNearest) {
    parsed.filter = MaterialSamplerFilter::Nearest;
  } else if (value == kFilterLinear) {
    parsed.filter = MaterialSamplerFilter::Linear;
  } else {
    return ResultT::Err(MaterialSourceParseError::UnknownFilter);
  }

  if (!matchField(lines[4], kAddressModePrefix, value)) {
    return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
  }
  if (value == kAddressModeRepeat) {
    parsed.addressMode = MaterialSamplerAddressMode::Repeat;
  } else if (value == kAddressModeClampToEdge) {
    parsed.addressMode = MaterialSamplerAddressMode::ClampToEdge;
  } else {
    return ResultT::Err(MaterialSourceParseError::UnknownAddressMode);
  }

  return ResultT::Ok(std::move(parsed));
}

std::string serializeMaterialSource(const ParsedMaterialSource& source) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kKindPrefix;
  // Plan 0019 P5: MaterialKind is a closed, two-enumerator vocabulary
  // now (ADR-0061 Decision 3) -- selected by source.kind, not hardcoded
  // to the single value this file's own first-draft precedent (ADR-0059
  // D2/D15, exactly one enumerator at the time) previously assumed.
  out += (source.kind == MaterialKind::UnlitTextured ? kKindUnlitTextured : kKindLitTextured);
  out += '\n';
  out += kTexturePrefix;
  out += source.textureLogicalPath;
  out += '\n';
  out += kFilterPrefix;
  out += (source.filter == MaterialSamplerFilter::Nearest ? kFilterNearest : kFilterLinear);
  out += '\n';
  out += kAddressModePrefix;
  out += (source.addressMode == MaterialSamplerAddressMode::Repeat ? kAddressModeRepeat : kAddressModeClampToEdge);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

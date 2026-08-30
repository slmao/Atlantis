#include <atlantis/asset_system/material_source.h>

#include <charconv>
#include <cstddef>
#include <vector>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_material_source_version: 2";
constexpr std::string_view kKindPrefix = "kind: ";
constexpr std::string_view kTexturePrefix = "texture: ";
constexpr std::string_view kFilterPrefix = "filter: ";
constexpr std::string_view kAddressModePrefix = "address_mode: ";
constexpr std::string_view kBaseColorFactorPrefix = "base_color_factor: ";
constexpr std::string_view kMetallicFactorPrefix = "metallic_factor: ";
constexpr std::string_view kRoughnessFactorPrefix = "roughness_factor: ";

constexpr std::string_view kKindUnlitTextured = "unlit_textured";
constexpr std::string_view kKindLitTextured = "lit_textured";
constexpr std::string_view kKindPbrDirectLit = "pbr_direct_lit";
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

// Duplicated from mesh_source.cpp's own identical helpers (Plan 0023
// Milestone 1), matching this file's own established "duplicated, not
// shared" precedent for this class of helper.
[[nodiscard]] std::vector<std::string_view> splitOnSpace(std::string_view s) {
  std::vector<std::string_view> tokens;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ' ') {
      tokens.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return tokens;
}

[[nodiscard]] bool parseFloatToken(std::string_view token, float& out) {
  if (token.empty()) return false;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out, std::chars_format::general);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

// Real, disclosed fix (found during Plan 0023's own final review):
// std::to_string(float) formats with a fixed 6 digits after the
// decimal point, which does NOT round-trip every float32 value back
// through parseFloatToken()'s own std::from_chars() exactly (e.g.
// 0.123456789f -> "0.123457" -> a DIFFERENT float32 bit pattern) --
// load_material.cpp's own exact-equality artifact-vs-metadata
// cross-validation shares this same risk, matching
// material_metadata.cpp's own identical fix. std::to_chars() with no
// explicit precision produces the shortest decimal string that
// round-trips back to the exact original bit pattern (the C++17
// <charconv> guarantee) -- symmetric with parseFloatToken()'s own
// std::from_chars() use above.
[[nodiscard]] std::string formatFloat(float value) {
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

}  // namespace

atlantis::Result<ParsedMaterialSource, MaterialSourceParseError> parseMaterialSource(std::string_view text) {
  using ResultT = atlantis::Result<ParsedMaterialSource, MaterialSourceParseError>;

  const std::vector<std::string_view> lines = splitLines(text);

  // Plan 0023 Milestone 1 (ADR-0066 item 2): version-2 grammar is either
  // exactly 5 lines (the three new numeric fields absent, defaults
  // apply) or exactly 8 lines (all three present, fixed order) -- no
  // partial subset, no dual-version reader.
  constexpr std::size_t kMinLineCount = 5;
  constexpr std::size_t kMaxLineCount = 8;
  if (lines.size() < kMinLineCount) return ResultT::Err(MaterialSourceParseError::MissingField);
  if (lines.size() > kMaxLineCount) return ResultT::Err(MaterialSourceParseError::TrailingContent);
  if (lines.size() != kMinLineCount && lines.size() != kMaxLineCount) {
    return ResultT::Err(MaterialSourceParseError::TrailingContent);
  }

  if (lines[0] != kVersionLine) return ResultT::Err(MaterialSourceParseError::UnknownSourceVersion);

  ParsedMaterialSource parsed;
  std::string_view value;

  if (!matchField(lines[1], kKindPrefix, value)) return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
  if (value == kKindUnlitTextured) {
    parsed.kind = MaterialKind::UnlitTextured;
  } else if (value == kKindLitTextured) {
    parsed.kind = MaterialKind::LitTextured;
  } else if (value == kKindPbrDirectLit) {
    parsed.kind = MaterialKind::PbrDirectLit;
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

  if (lines.size() == kMaxLineCount) {
    if (!matchField(lines[5], kBaseColorFactorPrefix, value)) {
      return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
    }
    const std::vector<std::string_view> baseColorTokens = splitOnSpace(value);
    if (baseColorTokens.size() != 4) return ResultT::Err(MaterialSourceParseError::MalformedNumber);
    for (std::size_t i = 0; i < 4; ++i) {
      if (!parseFloatToken(baseColorTokens[i], parsed.baseColorFactor[i])) {
        return ResultT::Err(MaterialSourceParseError::MalformedNumber);
      }
    }

    if (!matchField(lines[6], kMetallicFactorPrefix, value)) {
      return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
    }
    if (!parseFloatToken(value, parsed.metallicFactor)) return ResultT::Err(MaterialSourceParseError::MalformedNumber);

    if (!matchField(lines[7], kRoughnessFactorPrefix, value)) {
      return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
    }
    if (!parseFloatToken(value, parsed.roughnessFactor)) {
      return ResultT::Err(MaterialSourceParseError::MalformedNumber);
    }
  }

  return ResultT::Ok(std::move(parsed));
}

std::string serializeMaterialSource(const ParsedMaterialSource& source) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kKindPrefix;
  // Plan 0023 Milestone 1: MaterialKind is a closed, three-enumerator
  // vocabulary now (ADR-0066 item 1) -- selected by source.kind.
  if (source.kind == MaterialKind::UnlitTextured) {
    out += kKindUnlitTextured;
  } else if (source.kind == MaterialKind::LitTextured) {
    out += kKindLitTextured;
  } else {
    out += kKindPbrDirectLit;
  }
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
  // Plan 0023 Milestone 1: the three new numeric fields are always
  // serialized (round-trip testing exercises the 8-line form
  // exclusively; the 5-line, defaults-only form is exercised only by
  // parseMaterialSource() directly against a literal, never produced by
  // this function).
  out += kBaseColorFactorPrefix;
  for (std::size_t i = 0; i < 4; ++i) {
    if (i != 0) out += ' ';
    out += formatFloat(source.baseColorFactor[i]);
  }
  out += '\n';
  out += kMetallicFactorPrefix;
  out += formatFloat(source.metallicFactor);
  out += '\n';
  out += kRoughnessFactorPrefix;
  out += formatFloat(source.roughnessFactor);
  out += '\n';
  return out;
}

}  // namespace atlantis::asset_system

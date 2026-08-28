#include <atlantis/asset_system/mesh_source.h>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace atlantis::asset_system {

namespace {

// Plan 0020 Section P1/ADR-0063: version 3 -- position + color + UV0 +
// object-space normal, 11 fields per vertex line. Version 2 (8 fields,
// no normal) is rejected outright by the exact-string check below; no
// dual-version reader is implemented.
constexpr std::string_view kVersionLine = "atlantis_static_mesh_source_version: 3";
constexpr std::string_view kVertexCountPrefix = "vertex_count: ";
constexpr std::string_view kIndexCountPrefix = "index_count: ";
constexpr std::string_view kVertexPrefix = "vertex: ";
constexpr std::string_view kIndexPrefix = "index: ";
constexpr std::uint32_t kMaxVertexCount = 65535;

// Strips at most one trailing '\n' from the whole text (the grammar's
// own "one optional newline"), then splits on '\n'; each resulting line
// additionally has at most one trailing '\r' stripped (Windows
// core.autocrlf tolerance). Two consecutive trailing newlines therefore
// leave one genuine empty trailing line, correctly caught downstream as
// TrailingContent rather than silently accepted.
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

// Splits on exactly one ASCII space per delimiter -- consecutive spaces
// produce empty tokens, which downstream numeric parsing rejects, and a
// wrong field count is caught by the caller comparing tokens.size().
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

[[nodiscard]] bool parseUnsigned(std::string_view token, std::uint32_t& out) {
  if (token.empty()) return false;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

[[nodiscard]] bool parseFloatToken(std::string_view token, float& out) {
  if (token.empty()) return false;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out, std::chars_format::general);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

}  // namespace

namespace detail {

double computeNormalLengthSquared(float x, float y, float z) noexcept {
  const double xd = static_cast<double>(x);
  const double yd = static_cast<double>(y);
  const double zd = static_cast<double>(z);

  double lengthSquared = xd * xd;
  lengthSquared += yd * yd;
  lengthSquared += zd * zd;
  return lengthSquared;
}

bool isNormalLengthSquaredInTolerance(double lengthSquared) noexcept {
  return lengthSquared >= 0.9801 && lengthSquared <= 1.0201;
}

}  // namespace detail

atlantis::Result<ParsedMeshSource, SourceParseError> parseMeshSource(std::string_view text) {
  using ResultT = atlantis::Result<ParsedMeshSource, SourceParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  std::size_t lineIndex = 0;

  if (lineIndex >= lines.size() || lines[lineIndex] != kVersionLine) {
    return ResultT::Err(SourceParseError::UnknownSourceVersion);
  }
  ++lineIndex;

  if (lineIndex >= lines.size()) return ResultT::Err(SourceParseError::MissingField);
  if (lines[lineIndex].substr(0, kVertexCountPrefix.size()) != kVertexCountPrefix) {
    return ResultT::Err(SourceParseError::FieldOrderMismatch);
  }
  std::uint32_t vertexCount = 0;
  if (!parseUnsigned(lines[lineIndex].substr(kVertexCountPrefix.size()), vertexCount)) {
    return ResultT::Err(SourceParseError::MalformedNumber);
  }
  if (vertexCount == 0 || vertexCount > kMaxVertexCount) {
    return ResultT::Err(SourceParseError::VertexCountOutOfRange);
  }
  ++lineIndex;

  if (lineIndex >= lines.size()) return ResultT::Err(SourceParseError::MissingField);
  if (lines[lineIndex].substr(0, kIndexCountPrefix.size()) != kIndexCountPrefix) {
    return ResultT::Err(SourceParseError::FieldOrderMismatch);
  }
  std::uint32_t indexCount = 0;
  if (!parseUnsigned(lines[lineIndex].substr(kIndexCountPrefix.size()), indexCount)) {
    return ResultT::Err(SourceParseError::MalformedNumber);
  }
  if (indexCount == 0 || indexCount % 3 != 0) {
    return ResultT::Err(SourceParseError::IndexCountNotMultipleOfThree);
  }
  ++lineIndex;

  ParsedMeshSource parsed;
  parsed.vertices.reserve(vertexCount);
  for (std::uint32_t i = 0; i < vertexCount; ++i) {
    if (lineIndex >= lines.size()) return ResultT::Err(SourceParseError::MissingField);
    const std::string_view line = lines[lineIndex];
    if (line.substr(0, kVertexPrefix.size()) != kVertexPrefix) {
      return ResultT::Err(SourceParseError::FieldOrderMismatch);
    }
    const auto fields = splitOnSpace(line.substr(kVertexPrefix.size()));
    if (fields.size() != 11) return ResultT::Err(SourceParseError::CountMismatch);

    MeshSourceVertex vertex;
    float* const components[11] = {&vertex.positionX, &vertex.positionY, &vertex.positionZ, &vertex.colorR,
                                    &vertex.colorG,    &vertex.colorB,    &vertex.uvU,        &vertex.uvV,
                                    &vertex.normalX,   &vertex.normalY,   &vertex.normalZ};
    for (std::size_t f = 0; f < 11; ++f) {
      float value = 0.0f;
      if (!parseFloatToken(fields[f], value)) return ResultT::Err(SourceParseError::MalformedNumber);
      if (!std::isfinite(value)) return ResultT::Err(SourceParseError::NonFiniteFloat);
      *components[f] = value;
    }

    // Plan 0020 Section P7/Spec 0020 D3: the normal's own length-squared
    // check runs only after every one of this vertex's own 11
    // components (not merely the three normal ones) is already
    // confirmed finite, above -- matching the existing
    // per-float-then-per-vertex validation order this format already
    // established for position/color/UV0.
    const double lengthSquared = detail::computeNormalLengthSquared(vertex.normalX, vertex.normalY, vertex.normalZ);
    if (!detail::isNormalLengthSquaredInTolerance(lengthSquared)) {
      return ResultT::Err(SourceParseError::NonUnitNormal);
    }

    parsed.vertices.push_back(vertex);
    ++lineIndex;
  }

  // Bound before allocating: unlike vertex_count (already capped to the
  // std::uint16_t index domain above), index_count carries no upper
  // bound of its own -- a malformed text file declaring a huge
  // index_count (e.g. "index_count: 4000000000") would otherwise turn
  // parsed.indices.reserve(indexCount) into a multi-gigabyte allocation
  // attempt from a file that is actually tiny. A declared index_count
  // whose /3 line requirement exceeds the number of lines actually
  // remaining cannot possibly be satisfied -- reject as MissingField
  // now, before the allocation, rather than after failing partway
  // through the loop below.
  const std::size_t remainingLinesForIndices = lines.size() - lineIndex;
  if (static_cast<std::size_t>(indexCount) / 3 > remainingLinesForIndices) {
    return ResultT::Err(SourceParseError::MissingField);
  }

  parsed.indices.reserve(indexCount);
  for (std::uint32_t i = 0; i < indexCount / 3; ++i) {
    if (lineIndex >= lines.size()) return ResultT::Err(SourceParseError::MissingField);
    const std::string_view line = lines[lineIndex];
    if (line.substr(0, kIndexPrefix.size()) != kIndexPrefix) {
      return ResultT::Err(SourceParseError::FieldOrderMismatch);
    }
    const auto fields = splitOnSpace(line.substr(kIndexPrefix.size()));
    if (fields.size() != 3) return ResultT::Err(SourceParseError::CountMismatch);

    for (const auto& field : fields) {
      std::uint32_t index = 0;
      if (!parseUnsigned(field, index)) return ResultT::Err(SourceParseError::MalformedNumber);
      if (index >= vertexCount) return ResultT::Err(SourceParseError::IndexOutOfRange);
      parsed.indices.push_back(static_cast<std::uint16_t>(index));
    }
    ++lineIndex;
  }

  if (lineIndex != lines.size()) return ResultT::Err(SourceParseError::TrailingContent);

  return ResultT::Ok(std::move(parsed));
}

std::string serializeMeshSource(const ParsedMeshSource& source) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kVertexCountPrefix;
  out += std::to_string(source.vertices.size());
  out += '\n';
  out += kIndexCountPrefix;
  out += std::to_string(source.indices.size());
  out += '\n';

  for (const MeshSourceVertex& v : source.vertices) {
    out += kVertexPrefix;
    out += std::to_string(v.positionX) + ' ' + std::to_string(v.positionY) + ' ' + std::to_string(v.positionZ) +
           ' ' + std::to_string(v.colorR) + ' ' + std::to_string(v.colorG) + ' ' + std::to_string(v.colorB) + ' ' +
           std::to_string(v.uvU) + ' ' + std::to_string(v.uvV) + ' ' + std::to_string(v.normalX) + ' ' +
           std::to_string(v.normalY) + ' ' + std::to_string(v.normalZ);
    out += '\n';
  }

  for (std::size_t i = 0; i + 2 < source.indices.size(); i += 3) {
    out += kIndexPrefix;
    out += std::to_string(source.indices[i]) + ' ' + std::to_string(source.indices[i + 1]) + ' ' +
           std::to_string(source.indices[i + 2]);
    out += '\n';
  }

  return out;
}

}  // namespace atlantis::asset_system

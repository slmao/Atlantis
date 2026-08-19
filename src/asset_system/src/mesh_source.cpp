#include <atlantis/asset_system/mesh_source.h>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_static_mesh_source_version: 1";
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
    if (fields.size() != 6) return ResultT::Err(SourceParseError::CountMismatch);

    MeshSourceVertex vertex;
    float* const components[6] = {&vertex.positionX, &vertex.positionY, &vertex.positionZ,
                                   &vertex.colorR,    &vertex.colorG,    &vertex.colorB};
    for (std::size_t f = 0; f < 6; ++f) {
      float value = 0.0f;
      if (!parseFloatToken(fields[f], value)) return ResultT::Err(SourceParseError::MalformedNumber);
      if (!std::isfinite(value)) return ResultT::Err(SourceParseError::NonFiniteFloat);
      *components[f] = value;
    }
    parsed.vertices.push_back(vertex);
    ++lineIndex;
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
           ' ' + std::to_string(v.colorR) + ' ' + std::to_string(v.colorG) + ' ' + std::to_string(v.colorB);
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

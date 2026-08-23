#include <atlantis/asset_system/scene_source.h>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace atlantis::asset_system {

namespace {

constexpr std::string_view kVersionLine = "atlantis_scene_source_version: 1";
constexpr std::string_view kNodeCountPrefix = "node_count: ";
constexpr std::string_view kActiveCameraPrefix = "active_camera: ";
constexpr std::string_view kNodePrefix = "node: ";
constexpr std::string_view kNoneToken = "none";

// node: line field prefixes, fixed order (Plan 0015 Section D3's own
// literal grammar example) -- the first 11 tokens are always present;
// a 12th group (either exactly {"mesh="} or exactly {"camera_fov_y=",
// "camera_near_z=", "camera_far_z="}) is optional. position/rotation/
// scale each carry ONE prefix on their first value, followed by two
// bare floats -- matching MeshSourceVertex's own "vertex: x y z r g b"
// single-prefix-then-bare-values style; camera_fov_y/near_z/far_z are
// each individually prefixed, per D3's own example.
constexpr std::string_view kNodeIdPrefix = "node_id=";
constexpr std::string_view kParentPrefix = "parent=";
constexpr std::string_view kPositionPrefix = "position=";
constexpr std::string_view kRotationPrefix = "rotation=";
constexpr std::string_view kScalePrefix = "scale=";
constexpr std::string_view kMeshPrefix = "mesh=";
constexpr std::string_view kCameraFovYPrefix = "camera_fov_y=";
constexpr std::string_view kCameraNearZPrefix = "camera_near_z=";
constexpr std::string_view kCameraFarZPrefix = "camera_far_z=";

// mesh_source.cpp's own established helpers, duplicated here rather
// than shared -- both source files keep these file-local (anonymous
// namespace, not exported), matching that file's own precedent for
// exactly this class of small helper.
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

[[nodiscard]] bool consumePrefixedFloat(std::string_view token, std::string_view prefix, float& out) {
  if (token.substr(0, prefix.size()) != prefix) return false;
  return parseFloatToken(token.substr(prefix.size()), out);
}

}  // namespace

atlantis::Result<ParsedSceneSource, SceneSourceParseError> parseSceneSource(std::string_view text) {
  using ResultT = atlantis::Result<ParsedSceneSource, SceneSourceParseError>;

  const std::vector<std::string_view> lines = splitLines(text);
  std::size_t lineIndex = 0;

  if (lineIndex >= lines.size() || lines[lineIndex] != kVersionLine) {
    return ResultT::Err(SceneSourceParseError::UnknownSourceVersion);
  }
  ++lineIndex;

  if (lineIndex >= lines.size()) return ResultT::Err(SceneSourceParseError::MissingField);
  if (lines[lineIndex].substr(0, kNodeCountPrefix.size()) != kNodeCountPrefix) {
    return ResultT::Err(SceneSourceParseError::FieldOrderMismatch);
  }
  std::uint32_t nodeCount = 0;
  if (!parseUnsigned(lines[lineIndex].substr(kNodeCountPrefix.size()), nodeCount)) {
    return ResultT::Err(SceneSourceParseError::MalformedNumber);
  }
  ++lineIndex;

  if (lineIndex >= lines.size()) return ResultT::Err(SceneSourceParseError::MissingField);
  if (lines[lineIndex].substr(0, kActiveCameraPrefix.size()) != kActiveCameraPrefix) {
    return ResultT::Err(SceneSourceParseError::FieldOrderMismatch);
  }
  std::optional<std::uint32_t> activeCameraNodeId;
  {
    const std::string_view value = lines[lineIndex].substr(kActiveCameraPrefix.size());
    if (value != kNoneToken) {
      std::uint32_t parsedValue = 0;
      if (!parseUnsigned(value, parsedValue)) return ResultT::Err(SceneSourceParseError::InvalidParentToken);
      activeCameraNodeId = parsedValue;
    }
  }
  ++lineIndex;

  // Bound before allocating -- mirrors parseMeshSource()'s own
  // index_count guard (itself fixed in Plan 0012's own post-merge
  // review, D3's own note above): a declared node_count with no
  // plausible way to be satisfied by the lines actually remaining is
  // rejected now, before reserve(), rather than after failing partway
  // through the loop below.
  const std::size_t remainingLinesForNodes = lines.size() - lineIndex;
  if (nodeCount > remainingLinesForNodes) return ResultT::Err(SceneSourceParseError::MissingField);

  ParsedSceneSource parsed;
  parsed.activeCameraNodeId = activeCameraNodeId;
  parsed.nodes.reserve(nodeCount);

  for (std::uint32_t i = 0; i < nodeCount; ++i) {
    if (lineIndex >= lines.size()) return ResultT::Err(SceneSourceParseError::MissingField);
    const std::string_view line = lines[lineIndex];
    if (line.substr(0, kNodePrefix.size()) != kNodePrefix) {
      return ResultT::Err(SceneSourceParseError::FieldOrderMismatch);
    }
    const auto tokens = splitOnSpace(line.substr(kNodePrefix.size()));
    if (tokens.size() != 11 && tokens.size() != 12 && tokens.size() != 14) {
      return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
    }

    ParsedSceneNode node;

    if (tokens[0].substr(0, kNodeIdPrefix.size()) != kNodeIdPrefix) {
      return ResultT::Err(SceneSourceParseError::FieldOrderMismatch);
    }
    if (!parseUnsigned(tokens[0].substr(kNodeIdPrefix.size()), node.nodeId)) {
      return ResultT::Err(SceneSourceParseError::MalformedNumber);
    }

    if (tokens[1].substr(0, kParentPrefix.size()) != kParentPrefix) {
      return ResultT::Err(SceneSourceParseError::FieldOrderMismatch);
    }
    {
      const std::string_view value = tokens[1].substr(kParentPrefix.size());
      if (value != kNoneToken) {
        std::uint32_t parentId = 0;
        if (!parseUnsigned(value, parentId)) return ResultT::Err(SceneSourceParseError::InvalidParentToken);
        node.parentNodeId = parentId;
      }
    }

    // position=<f> <f> <f>  rotation=<f> <f> <f>  scale=<f> <f> <f> --
    // tokens[2..4], tokens[5..7], tokens[8..10]. Each group's first
    // token carries the prefix; the remaining two are bare floats,
    // matching MeshSourceVertex's own "vertex: x y z r g b" style.
    const struct {
      std::string_view prefix;
      float* x;
      float* y;
      float* z;
    } vec3Groups[3] = {
        {kPositionPrefix, &node.transform.positionX, &node.transform.positionY, &node.transform.positionZ},
        {kRotationPrefix, &node.transform.eulerXRadians, &node.transform.eulerYRadians,
         &node.transform.eulerZRadians},
        {kScalePrefix, &node.transform.scaleX, &node.transform.scaleY, &node.transform.scaleZ},
    };
    for (std::size_t g = 0; g < 3; ++g) {
      const std::size_t base = 2 + g * 3;
      if (tokens[base].substr(0, vec3Groups[g].prefix.size()) != vec3Groups[g].prefix) {
        return ResultT::Err(SceneSourceParseError::FieldOrderMismatch);
      }
      float x = 0.0f, y = 0.0f, z = 0.0f;
      if (!consumePrefixedFloat(tokens[base], vec3Groups[g].prefix, x) || !parseFloatToken(tokens[base + 1], y) ||
          !parseFloatToken(tokens[base + 2], z)) {
        return ResultT::Err(SceneSourceParseError::MalformedNumber);
      }
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return ResultT::Err(SceneSourceParseError::NonFiniteFloat);
      }
      *vec3Groups[g].x = x;
      *vec3Groups[g].y = y;
      *vec3Groups[g].z = z;
    }

    if (tokens.size() == 12) {
      if (tokens[11].substr(0, kMeshPrefix.size()) != kMeshPrefix) {
        return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
      }
      node.meshLogicalPath = std::string(tokens[11].substr(kMeshPrefix.size()));
      if (node.meshLogicalPath->empty()) return ResultT::Err(SceneSourceParseError::MissingField);
    } else if (tokens.size() == 14) {
      DecodedCamera camera;
      const std::pair<std::string_view, float*> cameraFields[3] = {
          {kCameraFovYPrefix, &camera.fovYRadians}, {kCameraNearZPrefix, &camera.nearZ},
          {kCameraFarZPrefix, &camera.farZ}};
      for (std::size_t f = 0; f < 3; ++f) {
        float value = 0.0f;
        if (!consumePrefixedFloat(tokens[11 + f], cameraFields[f].first, value)) {
          return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
        }
        if (!std::isfinite(value)) return ResultT::Err(SceneSourceParseError::NonFiniteFloat);
        *cameraFields[f].second = value;
      }
      node.camera = camera;
    }

    parsed.nodes.push_back(std::move(node));
    ++lineIndex;
  }

  if (lineIndex != lines.size()) return ResultT::Err(SceneSourceParseError::TrailingContent);

  return ResultT::Ok(std::move(parsed));
}

std::string serializeSceneSource(const ParsedSceneSource& source) {
  std::string out;
  out += kVersionLine;
  out += '\n';
  out += kNodeCountPrefix;
  out += std::to_string(source.nodes.size());
  out += '\n';
  out += kActiveCameraPrefix;
  out += source.activeCameraNodeId.has_value() ? std::to_string(*source.activeCameraNodeId)
                                                : std::string(kNoneToken);
  out += '\n';

  for (const ParsedSceneNode& node : source.nodes) {
    out += kNodePrefix;
    out += std::string(kNodeIdPrefix) + std::to_string(node.nodeId);
    out += ' ';
    out += std::string(kParentPrefix) +
           (node.parentNodeId.has_value() ? std::to_string(*node.parentNodeId) : std::string(kNoneToken));
    out += ' ';
    out += std::string(kPositionPrefix) + std::to_string(node.transform.positionX) + ' ' +
           std::to_string(node.transform.positionY) + ' ' + std::to_string(node.transform.positionZ);
    out += ' ';
    out += std::string(kRotationPrefix) + std::to_string(node.transform.eulerXRadians) + ' ' +
           std::to_string(node.transform.eulerYRadians) + ' ' + std::to_string(node.transform.eulerZRadians);
    out += ' ';
    out += std::string(kScalePrefix) + std::to_string(node.transform.scaleX) + ' ' +
           std::to_string(node.transform.scaleY) + ' ' + std::to_string(node.transform.scaleZ);

    if (node.meshLogicalPath.has_value()) {
      out += ' ';
      out += std::string(kMeshPrefix) + *node.meshLogicalPath;
    } else if (node.camera.has_value()) {
      out += ' ';
      out += std::string(kCameraFovYPrefix) + std::to_string(node.camera->fovYRadians) + ' ' +
             std::string(kCameraNearZPrefix) + std::to_string(node.camera->nearZ) + ' ' +
             std::string(kCameraFarZPrefix) + std::to_string(node.camera->farZ);
    }

    out += '\n';
  }

  return out;
}

}  // namespace atlantis::asset_system

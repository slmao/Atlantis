#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/scene_types.h>
#include <atlantis/result.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::asset_system {

// Plan 0015 Section D3: the authoring-facing, not-yet-cooked
// representation of one node -- node_id/parent are still author-
// assigned integers (not yet remapped to a dense array index, D4's own
// job), and a Renderable's mesh reference is still a logical-path
// string (not yet resolved to an AssetId, also D4's own job).
struct ParsedSceneNode {
  std::uint32_t nodeId = 0;
  std::optional<std::uint32_t> parentNodeId;  // std::nullopt for "parent=none"
  DecodedTransform transform;
  std::optional<DecodedCamera> camera;
  std::optional<std::string> meshLogicalPath;
};

struct ParsedSceneSource {
  std::vector<ParsedSceneNode> nodes;               // declaration order
  std::optional<std::uint32_t> activeCameraNodeId;  // std::nullopt for "active_camera: none"
};

// Plan 0015 Section D3: parse/decode-error conditions specific to the
// authoring grammar itself -- distinct from SceneCookError (cookScene()'s
// own, coarser classification; every one of these folds into
// SceneCookError::SourceParseFailed there), mirroring
// SourceParseError's own already-Accepted relationship to CookError for
// the mesh pipeline exactly.
enum class SceneSourceParseError {
  UnknownSourceVersion,
  MissingField,
  FieldOrderMismatch,
  MalformedNumber,
  NonFiniteFloat,
  InvalidParentToken,
  InvalidComponentGroup,
  TrailingContent,
};

// Strict, fixed-field-order, plain-text grammar extending
// mesh_source.h's own established style (anchored-prefix field
// matching, std::from_chars numeric parsing, no general parser
// library). Never rejects node_count == 0 by itself -- that semantic
// judgment (EmptyScene) belongs to cookScene(), checked immediately
// after a successful parse (Plan 0015 Section D4), so this function
// can be tested and reasoned about purely as a grammar, independent of
// that one scene-level policy.
[[nodiscard]] atlantis::Result<ParsedSceneSource, SceneSourceParseError> parseSceneSource(std::string_view text);

// Serializes back to the exact grammar parseSceneSource() accepts --
// exists for round-trip testing (V1), matching serializeMeshSource()'s
// own established role exactly.
[[nodiscard]] std::string serializeSceneSource(const ParsedSceneSource& source);

}  // namespace atlantis::asset_system

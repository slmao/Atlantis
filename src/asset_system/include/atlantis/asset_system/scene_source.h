#pragma once

#include <atlantis/asset_system/asset_guid.h>
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
// Plan 0018 Section P6: materialLogicalPath is only ever set alongside
// meshLogicalPath -- the version-2 grammar has no token arrangement
// that produces "material without mesh" (a structural, not runtime,
// guarantee; decodeSceneArtifact() independently re-checks the same
// invariant at the binary level, since it must never trust a
// well-formed producer -- see scene_artifact.cpp's own
// MaterialWithoutRenderable check).
// Plan 0047 P9 (scene source v7): entityGuid is the node's persistent
// identity (guid=, right after node_id=); it may be nil here -- the parser
// checks syntax only, and cookScene() rejects a nil or duplicate one. The
// mesh=/material= references are AssetGuids (ADR-0097 D3).
struct ParsedSceneNode {
  std::uint32_t nodeId = 0;
  EntityGuid entityGuid;
  std::optional<std::uint32_t> parentNodeId;  // std::nullopt for "parent=none"
  DecodedTransform transform;
  std::optional<DecodedCamera> camera;
  std::optional<AssetGuid> meshAsset;
  std::optional<AssetGuid> materialAsset;
  std::optional<DecodedLight> light;  // Spec 0019 D3: standalone -- never
                                        // co-present with camera/mesh on the
                                        // same node, a structural grammar
                                        // guarantee (P2)
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
  InvalidParentToken,
  InvalidComponentGroup,
  TrailingContent,
  // Spec 0019 D3/D11: a scene declaring more than 1 directional-kind or
  // more than 4 point-kind light nodes -- a whole-scene, post-node-
  // collection check, never a per-node one. A hard, structural error,
  // never a silent, deterministic truncation.
  TooManyLights,
  // Plan 0047 P9: a guid=, mesh= or material= value that is not a
  // canonical GUID (a nil mesh/material reference included).
  MalformedGuid,
};

// Strict, fixed-field-order, plain-text grammar extending
// mesh_source.h's own established style (anchored-prefix field
// matching, std::from_chars numeric parsing, no general parser
// library). Never rejects node_count == 0 by itself -- that semantic
// judgment (EmptyScene) belongs to cookScene(), checked immediately
// after a successful parse (Plan 0015 Section D4), so this function
// can be tested and reasoned about purely as a grammar, independent of
// that one scene-level policy. Deliberately does NOT reject a
// non-finite (nan/inf) float value either -- unlike mesh_source.h's
// own parseMeshSource(), Plan 0015 Section D4 assigns that check to
// cookScene() itself (its own step 7, SceneCookError::NonFiniteValue),
// not to the grammar layer; a syntactically well-formed "nan"/"inf"
// token parses successfully here.
[[nodiscard]] atlantis::Result<ParsedSceneSource, SceneSourceParseError> parseSceneSource(std::string_view text);

// Plan 0049 P5 (Spec 0049 R5/R6, ruling Q2): the scene semantic version this
// serializer (source v7) encodes -- see scene::kSemanticVersion in
// scene_semantic_schema.h. A semantic bump fails the conformance build until
// this declaration (and the format, per ADR-0045) follows.
inline constexpr std::uint32_t kSceneSourceSemanticVersion = 1;

// Serializes back to the exact grammar parseSceneSource() accepts --
// exists for round-trip testing (V1), matching serializeMeshSource()'s
// own established role exactly.
[[nodiscard]] std::string serializeSceneSource(const ParsedSceneSource& source);

}  // namespace atlantis::asset_system

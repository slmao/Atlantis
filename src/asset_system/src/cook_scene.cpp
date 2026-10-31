#include <atlantis/asset_system/cook_scene.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/asset_system/scene_source.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace atlantis::asset_system {

namespace {

namespace fs = std::filesystem;

// Duplicated from cook.cpp's own identical helpers rather than
// shared, matching that file's own file-local, not-exported
// precedent for exactly this class of helper.
[[nodiscard]] bool writeBytesAtomically(const fs::path& finalPath, const char* data, std::size_t size) {
  std::error_code ec;
  const fs::path dir = finalPath.parent_path();
  if (!dir.empty()) {
    fs::create_directories(dir, ec);
  }

  std::random_device rd;
  const fs::path tempPath =
      dir / (finalPath.filename().string() + ".tmp-" + std::to_string(rd()) + std::to_string(rd()));

  {
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(data, static_cast<std::streamsize>(size));
    out.flush();
    if (!out.good()) {
      out.close();
      fs::remove(tempPath, ec);
      return false;
    }
  }

  fs::rename(tempPath, finalPath, ec);
  if (ec) {
    fs::remove(tempPath, ec);
    return false;
  }
  return true;
}

[[nodiscard]] bool writeTextAtomically(const fs::path& finalPath, const std::string& text) {
  return writeBytesAtomically(finalPath, text.data(), text.size());
}

// Node_id-keyed ancestor walk -- a genuinely second, independent
// implementation of the same cycle-detection algorithm
// scene_artifact.cpp's own array-index-based hasCycleByIndex() uses
// (Plan 0015 Section D4 step 5 / D6 step 6), applied here to
// authoring-time node_id references rather than array indices. Only
// safe to call after every parent node_id is already confirmed
// declared (step 4).
//
// Three-state (Unvisited/Visiting/Done) iterative marking, matching
// hasCycleByIndex()'s own identical rationale: a naive fresh walk from
// every node is worst-case O(n^2) against a maliciously large,
// pathologically-chained authoring source. Marking every node_id Done
// exactly once bounds total work to O(n) map operations.
[[nodiscard]] bool hasCycleByNodeId(const std::unordered_map<std::uint32_t, std::optional<std::uint32_t>>& parentOf) {
  enum class State : std::uint8_t { Unvisited, Visiting, Done };
  std::unordered_map<std::uint32_t, State> state;
  state.reserve(parentOf.size());
  std::vector<std::uint32_t> path;

  for (const auto& [startId, _] : parentOf) {
    if (state[startId] != State::Unvisited) continue;

    path.clear();
    std::uint32_t current = startId;
    while (true) {
      State& currentState = state[current];
      if (currentState == State::Visiting) return true;
      if (currentState == State::Done) break;
      currentState = State::Visiting;
      path.push_back(current);
      const std::optional<std::uint32_t>& parent = parentOf.at(current);
      if (!parent.has_value()) break;
      current = *parent;
    }
    for (std::uint32_t node : path) state[node] = State::Done;
  }
  return false;
}

// Plan 0065 P1: steps 2-10 of the cook -- every check after a successful parse,
// then the artifact encode and the sidecar text. Shared by cookSceneSource()
// (and so cookScene()) and cookSceneToMemory(), which parses the text itself so
// that its round-trip check can reuse the parse.
[[nodiscard]] atlantis::Result<CookedScene, SceneCookError> cookParsedSceneSource(const ParsedSceneSource& parsed,
                                                                                   const AssetGuid& assetGuid) {
  using ResultT = atlantis::Result<CookedScene, SceneCookError>;

  // Step 2: EmptyScene.
  if (parsed.nodes.empty()) return ResultT::Err(SceneCookError::EmptyScene);

  // Spec 0074: refuse before derived allocations or narrowing the count;
  // a successful cook must fit the decoder's existing format capacity.
  if (parsed.nodes.size() > kMaxSceneArtifactNodeCount) return ResultT::Err(SceneCookError::NodeCountOutOfRange);

  // Step 3: duplicate node_id -- O(n log n) via sort + adjacency.
  {
    std::vector<std::uint32_t> ids;
    ids.reserve(parsed.nodes.size());
    for (const ParsedSceneNode& node : parsed.nodes) ids.push_back(node.nodeId);
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 1; i < ids.size(); ++i) {
      if (ids[i] == ids[i - 1]) return ResultT::Err(SceneCookError::DuplicateNodeId);
    }
  }

  // Step 3b (Plan 0047 P9, ADR-0097 D5): every EntityGuid non-nil and
  // unique within the scene.
  {
    std::vector<EntityGuid> guids;
    guids.reserve(parsed.nodes.size());
    for (const ParsedSceneNode& node : parsed.nodes) {
      if (node.entityGuid == EntityGuid{}) return ResultT::Err(SceneCookError::NilEntityGuid);
      guids.push_back(node.entityGuid);
    }
    std::sort(guids.begin(), guids.end());
    if (std::adjacent_find(guids.begin(), guids.end()) != guids.end()) {
      return ResultT::Err(SceneCookError::DuplicateEntityGuid);
    }
  }

  // node_id -> declaration-order array index, and node_id -> its own
  // parent node_id -- built once duplicates are ruled out, reused by
  // steps 4-6 and step 9's own remapping below.
  std::unordered_map<std::uint32_t, std::size_t> idToIndex;
  std::unordered_map<std::uint32_t, std::optional<std::uint32_t>> parentOfId;
  idToIndex.reserve(parsed.nodes.size());
  parentOfId.reserve(parsed.nodes.size());
  for (std::size_t i = 0; i < parsed.nodes.size(); ++i) {
    idToIndex[parsed.nodes[i].nodeId] = i;
    parentOfId[parsed.nodes[i].nodeId] = parsed.nodes[i].parentNodeId;
  }

  // Step 4: undeclared parent reference.
  for (const ParsedSceneNode& node : parsed.nodes) {
    if (node.parentNodeId.has_value() && !idToIndex.contains(*node.parentNodeId)) {
      return ResultT::Err(SceneCookError::UndeclaredParentReference);
    }
  }

  // Step 5: parent cycle, node_id-keyed.
  if (hasCycleByNodeId(parentOfId)) return ResultT::Err(SceneCookError::ParentCycle);

  // Step 6 (reference half): active_camera's own node_id must be
  // declared. The "that node carries a Camera" half is checked below,
  // once the per-node loop has built each node's own ValidatedSceneNode.
  if (parsed.activeCameraNodeId.has_value() && !idToIndex.contains(*parsed.activeCameraNodeId)) {
    return ResultT::Err(SceneCookError::UndeclaredActiveCameraReference);
  }

  // Steps 7-9: non-finite check, mesh reference resolution, and dense
  // remapping -- one pass, since each is a per-node operation over the
  // same declaration-order array parseSceneSource() already produced.
  std::vector<ValidatedSceneNode> nodes;
  std::vector<std::optional<std::size_t>> parents;
  std::vector<EntityGuid> entityGuids;
  nodes.reserve(parsed.nodes.size());
  parents.reserve(parsed.nodes.size());
  entityGuids.reserve(parsed.nodes.size());

  for (const ParsedSceneNode& parsedNode : parsed.nodes) {
    const float transformFloats[9] = {
        parsedNode.transform.positionX,     parsedNode.transform.positionY,  parsedNode.transform.positionZ,
        parsedNode.transform.eulerXRadians, parsedNode.transform.eulerYRadians,
        parsedNode.transform.eulerZRadians, parsedNode.transform.scaleX,     parsedNode.transform.scaleY,
        parsedNode.transform.scaleZ};
    for (float value : transformFloats) {
      if (!std::isfinite(value)) return ResultT::Err(SceneCookError::NonFiniteValue);
    }

    ValidatedSceneNode node;
    node.transform = parsedNode.transform;

    if (parsedNode.camera.has_value()) {
      if (!std::isfinite(parsedNode.camera->fovYRadians) || !std::isfinite(parsedNode.camera->nearZ) ||
          !std::isfinite(parsedNode.camera->farZ) ||
          !std::isfinite(parsedNode.camera->exposureCompensationEv) ||
          parsedNode.camera->exposureCompensationEv < kExposureCompensationEvMin ||
          parsedNode.camera->exposureCompensationEv > kExposureCompensationEvMax ||
          !isValidCameraFog(parsedNode.camera->fog) ||  // Plan 0043 P2: NonFiniteValue reused, like exposure
          !isValidCameraBloom(parsedNode.camera->bloom)) {  // Plan 0044 P2: likewise
        return ResultT::Err(SceneCookError::NonFiniteValue);
      }
      node.camera = parsedNode.camera;
    }

    // Spec 0019 D3: parseSceneSource() has already fully validated every
    // light field (finite, value-domain) by the time cookScene() runs --
    // this redundant re-check mirrors the identical, already-established
    // camera/transform precedent immediately above (defense in depth,
    // not dead code).
    if (parsedNode.light.has_value()) {
      if (!std::isfinite(parsedNode.light->colorR) || !std::isfinite(parsedNode.light->colorG) ||
          !std::isfinite(parsedNode.light->colorB) || !std::isfinite(parsedNode.light->intensity) ||
          !std::isfinite(parsedNode.light->range)) {
        return ResultT::Err(SceneCookError::NonFiniteValue);
      }
      node.light = parsedNode.light;
    }

    // Plan 0047 P9 (ADR-0097 D2/D3): references are GUIDs; the artifact
    // carries their 64-bit keys. Value-level only, never an existence
    // check (ADR-0059 D6/D7). materialAsset is only ever set alongside
    // meshAsset (Plan 0018 Section P6's grammar-structural guarantee).
    if (parsedNode.meshAsset.has_value()) {
      DecodedRenderable renderable;
      renderable.meshAsset = assetKey(*parsedNode.meshAsset);
      if (parsedNode.materialAsset.has_value()) renderable.materialAsset = assetKey(*parsedNode.materialAsset);
      node.renderable = renderable;
    }

    entityGuids.push_back(parsedNode.entityGuid);
    nodes.push_back(std::move(node));
    parents.push_back(parsedNode.parentNodeId.has_value() ? std::optional<std::size_t>(idToIndex[*parsedNode.parentNodeId])
                                                            : std::nullopt);
  }

  std::optional<std::size_t> activeCameraIndex;
  if (parsed.activeCameraNodeId.has_value()) {
    const std::size_t index = idToIndex[*parsed.activeCameraNodeId];
    if (!nodes[index].camera.has_value()) return ResultT::Err(SceneCookError::ActiveCameraMissingCamera);
    activeCameraIndex = index;
  }

  // Step 10: encode (cookScene() then writes both atomically).
  CookedScene cooked;
  cooked.artifactBytes = encodeSceneArtifact(nodes, parents, activeCameraIndex, entityGuids);

  SceneMetadata metadata;
  metadata.assetGuid = assetGuid;
  metadata.schemaVersion = kSceneArtifactSchemaVersion;
  metadata.nodeCount = static_cast<std::uint32_t>(nodes.size());
  cooked.metadataText = serializeSceneMetadata(metadata);

  return ResultT::Ok(std::move(cooked));
}

// Plan 0065 P1: the mapping's reference errors, as the cooker names the same
// conditions. toParsedSceneSource() raises only the Dangling* and Ambiguous*
// ones; the others are mapped for completeness.
[[nodiscard]] SceneCookError toSceneCookError(scene::SceneMappingError error) {
  switch (error) {
    case scene::SceneMappingError::DuplicateNodeId:
      return SceneCookError::DuplicateNodeId;
    case scene::SceneMappingError::UndeclaredParentReference:
    case scene::SceneMappingError::DanglingParentReference:
      return SceneCookError::UndeclaredParentReference;
    case scene::SceneMappingError::UndeclaredActiveCameraReference:
    case scene::SceneMappingError::DanglingActiveCameraReference:
      return SceneCookError::UndeclaredActiveCameraReference;
    case scene::SceneMappingError::AmbiguousEntityGuid:
      return SceneCookError::DuplicateEntityGuid;
    case scene::SceneMappingError::MaterialWithoutMesh:
      return SceneCookError::NotRepresentableInSource;
  }
  return SceneCookError::NotRepresentableInSource;
}

}  // namespace

atlantis::Result<CookedScene, SceneCookError> cookSceneSource(std::string_view sourceText, const AssetGuid& assetGuid) {
  using ResultT = atlantis::Result<CookedScene, SceneCookError>;
  ATLANTIS_CHECK_MSG(assetGuid != AssetGuid{}, "cookSceneSource(): the asset GUID must not be nil");

  // Step 1 (the parse).
  const auto parsedResult = parseSceneSource(sourceText);
  if (parsedResult.isErr()) return ResultT::Err(SceneCookError::SourceParseFailed);
  return cookParsedSceneSource(parsedResult.value(), assetGuid);
}

atlantis::Result<CookedScene, SceneCookError> cookSceneToMemory(const scene::AuthoringScene& document,
                                                               const AssetGuid& assetGuid) {
  using ResultT = atlantis::Result<CookedScene, SceneCookError>;
  ATLANTIS_CHECK_MSG(assetGuid != AssetGuid{}, "cookSceneToMemory(): the asset GUID must not be nil");

  const auto mapped = scene::toParsedSceneSource(document);
  if (mapped.isErr()) return ResultT::Err(toSceneCookError(mapped.error()));

  // The build's text path: the saved text, parsed and cooked exactly as
  // cookSceneSource() does (inlined so the round trip below reuses the parse).
  const std::string text = serializeSceneSource(mapped.value());
  const auto parsed = parseSceneSource(text);
  if (parsed.isErr()) return ResultT::Err(SceneCookError::SourceParseFailed);
  auto cooked = cookParsedSceneSource(parsed.value(), assetGuid);
  if (cooked.isErr()) return cooked;

  // The round-trip check: the text must hold the whole document.
  const auto roundTrip = scene::toAuthoringScene(parsed.value());
  if (roundTrip.isErr() || !(roundTrip.value() == document)) {
    return ResultT::Err(SceneCookError::NotRepresentableInSource);
  }
  return cooked;
}

atlantis::Result<std::monostate, SceneCookError> cookScene(const std::string& sourceFilePath,
                                                             const AssetGuid& assetGuid,
                                                             const std::string& artifactOutputPath,
                                                             const std::string& metadataOutputPath) {
  using ResultT = atlantis::Result<std::monostate, SceneCookError>;
  ATLANTIS_CHECK_MSG(assetGuid != AssetGuid{}, "cookScene(): the asset GUID must not be nil");

  // Step 1: read; the parse and steps 2-10 are cookSceneSource()'s.
  std::ifstream sourceFile(sourceFilePath, std::ios::binary);
  if (!sourceFile.is_open()) return ResultT::Err(SceneCookError::SourceFileUnreadable);
  std::ostringstream sourceStream;
  sourceStream << sourceFile.rdbuf();
  if (sourceFile.bad()) return ResultT::Err(SceneCookError::SourceFileUnreadable);
  const std::string sourceText = sourceStream.str();

  const auto cooked = cookSceneSource(sourceText, assetGuid);
  if (cooked.isErr()) return ResultT::Err(cooked.error());

  // Atomic writes.
  const std::vector<std::byte>& artifactBytes = cooked.value().artifactBytes;
  if (!writeBytesAtomically(artifactOutputPath, reinterpret_cast<const char*>(artifactBytes.data()),
                             artifactBytes.size())) {
    return ResultT::Err(SceneCookError::ArtifactWriteFailed);
  }
  if (!writeTextAtomically(metadataOutputPath, cooked.value().metadataText)) {
    return ResultT::Err(SceneCookError::MetadataWriteFailed);
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system

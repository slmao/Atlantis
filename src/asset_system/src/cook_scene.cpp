#include <atlantis/asset_system/cook_scene.h>

#include <atlantis/asset_system/asset_id.h>
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

}  // namespace

atlantis::Result<std::monostate, SceneCookError> cookScene(const std::string& sourceFilePath,
                                                             const std::string& artifactOutputPath,
                                                             const std::string& metadataOutputPath) {
  using ResultT = atlantis::Result<std::monostate, SceneCookError>;

  // Step 1: read + parse.
  std::ifstream sourceFile(sourceFilePath, std::ios::binary);
  if (!sourceFile.is_open()) return ResultT::Err(SceneCookError::SourceFileUnreadable);
  std::ostringstream sourceStream;
  sourceStream << sourceFile.rdbuf();
  if (sourceFile.bad()) return ResultT::Err(SceneCookError::SourceFileUnreadable);
  const std::string sourceText = sourceStream.str();

  const auto parsedResult = parseSceneSource(sourceText);
  if (parsedResult.isErr()) return ResultT::Err(SceneCookError::SourceParseFailed);
  const ParsedSceneSource& parsed = parsedResult.value();

  // Step 2: EmptyScene.
  if (parsed.nodes.empty()) return ResultT::Err(SceneCookError::EmptyScene);

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
  nodes.reserve(parsed.nodes.size());
  parents.reserve(parsed.nodes.size());

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
          !std::isfinite(parsedNode.camera->farZ)) {
        return ResultT::Err(SceneCookError::NonFiniteValue);
      }
      node.camera = parsedNode.camera;
    }

    if (parsedNode.meshLogicalPath.has_value()) {
      const auto normalizedResult = normalizeLogicalPath(*parsedNode.meshLogicalPath);
      if (normalizedResult.isErr()) return ResultT::Err(SceneCookError::SourceParseFailed);
      node.renderable = DecodedRenderable{computeAssetId(normalizedResult.value())};
    }

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

  // Step 10: encode + atomic write.
  const std::vector<std::byte> artifactBytes = encodeSceneArtifact(nodes, parents, activeCameraIndex);

  SceneMetadata metadata;
  metadata.schemaVersion = kSceneArtifactSchemaVersion;
  metadata.nodeCount = static_cast<std::uint32_t>(nodes.size());
  const std::string metadataText = serializeSceneMetadata(metadata);

  if (!writeBytesAtomically(artifactOutputPath, reinterpret_cast<const char*>(artifactBytes.data()),
                             artifactBytes.size())) {
    return ResultT::Err(SceneCookError::ArtifactWriteFailed);
  }
  if (!writeTextAtomically(metadataOutputPath, metadataText)) {
    return ResultT::Err(SceneCookError::MetadataWriteFailed);
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system

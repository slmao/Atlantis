#include <atlantis/asset_system/scene_artifact.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace atlantis::asset_system {

namespace {

constexpr std::array<char, 4> kMagic = {'A', 'S', 'C', 'N'};

// Duplicated from mesh_artifact.cpp's own identical helpers rather
// than shared, matching that file's own file-local, not-exported
// precedent.
void appendU32LE(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

void appendU64LE(std::vector<std::byte>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
}

void appendFloatLE(std::vector<std::byte>& out, float value) { appendU32LE(out, std::bit_cast<std::uint32_t>(value)); }

[[nodiscard]] std::uint32_t readU32LE(const std::byte* bytes) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
  return value;
}

[[nodiscard]] std::uint64_t readU64LE(const std::byte* bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  return value;
}

[[nodiscard]] float readFloatLE(const std::byte* bytes) { return std::bit_cast<float>(readU32LE(bytes)); }

// Array-index-based ancestor walk -- a second, independent instance of
// the same cycle-detection algorithm cookScene()'s own node_id-keyed
// walk uses (Plan 0015 Section D4 step 5 / D6 step 6), applied here to
// already-range-checked array indices. Only safe to call after every
// parent index has already been confirmed < parents.size().
//
// Three-state (Unvisited/Visiting/Done) iterative marking, not a naive
// per-node ancestor walk: a naive walk starting fresh from every node
// is worst-case O(n^2) (e.g. a long acyclic chain with no shared
// suffix caching) -- for a decode path that must never trust a
// declared node_count up to kMaxSceneArtifactNodeCount, that is a real
// hardening gap against a maliciously crafted artifact, not merely a
// style preference. Marking every node Done exactly once means the
// total work across all starting points is O(n): a node already Done
// short-circuits immediately, and re-encountering a node already
// Visiting in the CURRENT walk is exactly a cycle.
[[nodiscard]] bool hasCycleByIndex(const std::vector<std::optional<std::size_t>>& parents) {
  enum class State : std::uint8_t { Unvisited, Visiting, Done };
  const std::size_t n = parents.size();
  std::vector<State> state(n, State::Unvisited);
  std::vector<std::size_t> path;

  for (std::size_t start = 0; start < n; ++start) {
    if (state[start] != State::Unvisited) continue;

    path.clear();
    std::size_t current = start;
    while (true) {
      if (state[current] == State::Visiting) return true;
      if (state[current] == State::Done) break;
      state[current] = State::Visiting;
      path.push_back(current);
      if (!parents[current].has_value()) break;
      current = *parents[current];
    }
    for (std::size_t node : path) state[node] = State::Done;
  }
  return false;
}

}  // namespace

std::vector<std::byte> encodeSceneArtifact(const std::vector<ValidatedSceneNode>& nodes,
                                            const std::vector<std::optional<std::size_t>>& parents,
                                            std::optional<std::size_t> activeCameraIndex) {
  std::vector<std::byte> out;
  out.reserve(kSceneArtifactHeaderSizeBytes + nodes.size() * kSceneArtifactNodeRecordSizeBytes);

  for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
  appendU32LE(out, kSceneArtifactSchemaVersion);
  appendU32LE(out, static_cast<std::uint32_t>(nodes.size()));
  appendU32LE(out, activeCameraIndex.has_value() ? 1U : 0U);
  appendU32LE(out, activeCameraIndex.has_value() ? static_cast<std::uint32_t>(*activeCameraIndex) : 0U);
  appendU32LE(out, 0U);  // reserved

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const ValidatedSceneNode& node = nodes[i];
    appendFloatLE(out, node.transform.positionX);
    appendFloatLE(out, node.transform.positionY);
    appendFloatLE(out, node.transform.positionZ);
    appendFloatLE(out, node.transform.eulerXRadians);
    appendFloatLE(out, node.transform.eulerYRadians);
    appendFloatLE(out, node.transform.eulerZRadians);
    appendFloatLE(out, node.transform.scaleX);
    appendFloatLE(out, node.transform.scaleY);
    appendFloatLE(out, node.transform.scaleZ);

    appendU32LE(out, node.camera.has_value() ? 1U : 0U);
    appendFloatLE(out, node.camera.has_value() ? node.camera->fovYRadians : 0.0f);
    appendFloatLE(out, node.camera.has_value() ? node.camera->nearZ : 0.0f);
    appendFloatLE(out, node.camera.has_value() ? node.camera->farZ : 0.0f);

    appendU32LE(out, node.renderable.has_value() ? 1U : 0U);
    appendU64LE(out, node.renderable.has_value() ? node.renderable->meshAsset : 0U);

    const bool hasMaterial = node.renderable.has_value() && node.renderable->materialAsset.has_value();
    appendU32LE(out, hasMaterial ? 1U : 0U);
    appendU64LE(out, hasMaterial ? *node.renderable->materialAsset : 0U);

    appendU32LE(out, node.light.has_value() ? 1U : 0U);
    appendU32LE(out, node.light.has_value() ? static_cast<std::uint32_t>(node.light->kind) : 0U);
    appendFloatLE(out, node.light.has_value() ? node.light->colorR : 0.0f);
    appendFloatLE(out, node.light.has_value() ? node.light->colorG : 0.0f);
    appendFloatLE(out, node.light.has_value() ? node.light->colorB : 0.0f);
    appendFloatLE(out, node.light.has_value() ? node.light->intensity : 0.0f);
    appendFloatLE(out, node.light.has_value() ? node.light->range : 0.0f);

    appendU32LE(out, parents[i].has_value() ? 1U : 0U);
    appendU32LE(out, parents[i].has_value() ? static_cast<std::uint32_t>(*parents[i]) : 0U);
  }

  return out;
}

atlantis::Result<DecodedSceneArtifact, SceneArtifactDecodeError> decodeSceneArtifact(
    const std::vector<std::byte>& bytes) {
  using ResultT = atlantis::Result<DecodedSceneArtifact, SceneArtifactDecodeError>;

  if (bytes.size() < kSceneArtifactHeaderSizeBytes) return ResultT::Err(SceneArtifactDecodeError::TooSmallForHeader);

  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (bytes[i] != static_cast<std::byte>(kMagic[i])) return ResultT::Err(SceneArtifactDecodeError::BadMagic);
  }

  const std::uint32_t schemaVersion = readU32LE(bytes.data() + 4);
  if (schemaVersion != kSceneArtifactSchemaVersion) {
    return ResultT::Err(SceneArtifactDecodeError::UnknownSchemaVersion);
  }

  const std::uint32_t nodeCount = readU32LE(bytes.data() + 8);
  const std::uint32_t hasActiveCameraFlag = readU32LE(bytes.data() + 12);
  const std::uint32_t activeCameraIndexRaw = readU32LE(bytes.data() + 16);
  // Byte 20..23 ("reserved") is intentionally never read.

  // Step 3: EmptyScene, re-checked independently of the cooker.
  if (nodeCount == 0) return ResultT::Err(SceneArtifactDecodeError::EmptyScene);

  // Step 4: bound before allocating -- never trust a declared count
  // enough to allocate on its word alone.
  if (nodeCount > kMaxSceneArtifactNodeCount) return ResultT::Err(SceneArtifactDecodeError::NodeCountOutOfRange);

  const std::uint64_t expectedSize =
      static_cast<std::uint64_t>(kSceneArtifactHeaderSizeBytes) +
      static_cast<std::uint64_t>(nodeCount) * kSceneArtifactNodeRecordSizeBytes;
  if (static_cast<std::uint64_t>(bytes.size()) != expectedSize) {
    return ResultT::Err(SceneArtifactDecodeError::SizeMismatch);
  }

  DecodedSceneArtifact decoded;
  decoded.nodes.reserve(nodeCount);
  decoded.parents.reserve(nodeCount);

  // Step 5: per-node decode -- structural (parent index range) and
  // finite-value checks.
  for (std::uint32_t i = 0; i < nodeCount; ++i) {
    const std::byte* record =
        bytes.data() + kSceneArtifactHeaderSizeBytes + static_cast<std::size_t>(i) * kSceneArtifactNodeRecordSizeBytes;

    ValidatedSceneNode node;
    node.transform.positionX = readFloatLE(record + 0);
    node.transform.positionY = readFloatLE(record + 4);
    node.transform.positionZ = readFloatLE(record + 8);
    node.transform.eulerXRadians = readFloatLE(record + 12);
    node.transform.eulerYRadians = readFloatLE(record + 16);
    node.transform.eulerZRadians = readFloatLE(record + 20);
    node.transform.scaleX = readFloatLE(record + 24);
    node.transform.scaleY = readFloatLE(record + 28);
    node.transform.scaleZ = readFloatLE(record + 32);

    const float transformFloats[9] = {node.transform.positionX, node.transform.positionY, node.transform.positionZ,
                                       node.transform.eulerXRadians, node.transform.eulerYRadians,
                                       node.transform.eulerZRadians, node.transform.scaleX, node.transform.scaleY,
                                       node.transform.scaleZ};
    for (float value : transformFloats) {
      if (!std::isfinite(value)) return ResultT::Err(SceneArtifactDecodeError::NonFiniteValue);
    }

    const std::uint32_t hasCameraFlag = readU32LE(record + 36);
    const float fovY = readFloatLE(record + 40);
    const float nearZ = readFloatLE(record + 44);
    const float farZ = readFloatLE(record + 48);
    if (hasCameraFlag != 0) {
      if (!std::isfinite(fovY) || !std::isfinite(nearZ) || !std::isfinite(farZ)) {
        return ResultT::Err(SceneArtifactDecodeError::NonFiniteValue);
      }
      node.camera = DecodedCamera{fovY, nearZ, farZ};
    }

    const std::uint32_t hasRenderableFlag = readU32LE(record + 52);
    const std::uint64_t meshAssetId = readU64LE(record + 56);

    const std::uint32_t hasMaterialFlag = readU32LE(record + 64);
    const std::uint64_t materialAssetId = readU64LE(record + 68);
    // Plan 0018 Section P7: independent, never-trust-the-cooker check --
    // a material reference with no renderable is a structurally
    // impossible combination this grammar can never author, but decode
    // must reject it explicitly, not silently ignore it.
    if (hasMaterialFlag != 0 && hasRenderableFlag == 0) {
      return ResultT::Err(SceneArtifactDecodeError::MaterialWithoutRenderable);
    }

    if (hasRenderableFlag != 0) {
      node.renderable = DecodedRenderable{meshAssetId, hasMaterialFlag != 0
                                                            ? std::optional<AssetId>(materialAssetId)
                                                            : std::optional<AssetId>(std::nullopt)};
    }

    // Spec 0019 D3/P4: light slot, inserted after material, before
    // parent -- independently re-validated here, never trusting the
    // cooker (parseSceneSource()'s own already-performed check).
    const std::uint32_t hasLightFlag = readU32LE(record + 76);
    const std::uint32_t lightKindRaw = readU32LE(record + 80);
    const float colorR = readFloatLE(record + 84);
    const float colorG = readFloatLE(record + 88);
    const float colorB = readFloatLE(record + 92);
    const float intensity = readFloatLE(record + 96);
    const float range = readFloatLE(record + 100);
    if (hasLightFlag != 0) {
      if (lightKindRaw != 0 && lightKindRaw != 1) return ResultT::Err(SceneArtifactDecodeError::NonFiniteValue);
      const bool isPoint = lightKindRaw == 1;
      if (!std::isfinite(colorR) || colorR < 0.0f || colorR > 1.0f || !std::isfinite(colorG) || colorG < 0.0f ||
          colorG > 1.0f || !std::isfinite(colorB) || colorB < 0.0f || colorB > 1.0f || !std::isfinite(intensity) ||
          intensity < 0.0f || (isPoint && (!std::isfinite(range) || range <= 0.0f)) ||
          (!isPoint && range != 0.0f)) {
        return ResultT::Err(SceneArtifactDecodeError::NonFiniteValue);
      }
      node.light = DecodedLight{isPoint ? DecodedLightKind::Point : DecodedLightKind::Directional, colorR, colorG,
                                 colorB, intensity, isPoint ? range : 0.0f};
    }

    const std::uint32_t hasParentFlag = readU32LE(record + 104);
    const std::uint32_t parentIndex = readU32LE(record + 108);
    std::optional<std::size_t> parent;
    if (hasParentFlag != 0) {
      if (parentIndex >= nodeCount) return ResultT::Err(SceneArtifactDecodeError::OutOfRangeParentIndex);
      parent = parentIndex;
    }

    decoded.nodes.push_back(std::move(node));
    decoded.parents.push_back(parent);
  }

  // Spec 0019 D3/finding 4: the light-count cap, independently
  // re-counted here -- never trusting the cooker's own already-performed
  // check.
  {
    std::uint32_t directionalCount = 0;
    std::uint32_t pointCount = 0;
    for (const auto& node : decoded.nodes) {
      if (!node.light.has_value()) continue;
      if (node.light->kind == DecodedLightKind::Directional) {
        ++directionalCount;
      } else {
        ++pointCount;
      }
    }
    if (directionalCount > 1 || pointCount > 4) return ResultT::Err(SceneArtifactDecodeError::TooManyLights);
  }

  // Step 6: cycle re-check, array-index-based -- safe now that every
  // parent index above is already confirmed in range.
  if (hasCycleByIndex(decoded.parents)) return ResultT::Err(SceneArtifactDecodeError::CyclicParent);

  // Step 7: active-camera range and Camera-presence check.
  if (hasActiveCameraFlag != 0) {
    if (activeCameraIndexRaw >= nodeCount) {
      return ResultT::Err(SceneArtifactDecodeError::OutOfRangeActiveCameraIndex);
    }
    if (!decoded.nodes[activeCameraIndexRaw].camera.has_value()) {
      return ResultT::Err(SceneArtifactDecodeError::ActiveCameraMissingCamera);
    }
    decoded.activeCameraIndex = activeCameraIndexRaw;
  }

  return ResultT::Ok(std::move(decoded));
}

}  // namespace atlantis::asset_system

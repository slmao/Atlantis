#include <atlantis/asset_system/decode_scene.h>

#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_metadata.h>

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace atlantis::asset_system {

namespace {

// Duplicated from load.cpp's own identical helpers rather than
// shared, matching that file's own file-local, not-exported
// precedent.
[[nodiscard]] bool readFileBytes(const std::string& path, std::vector<std::byte>& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) return false;
  out.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!out.empty()) {
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  }
  return static_cast<bool>(file) || file.eof();
}

[[nodiscard]] bool readFileText(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) return false;
  out = buffer.str();
  return true;
}

}  // namespace

atlantis::Result<ValidatedSceneData, SceneArtifactDecodeError> decodeScene(const std::string& artifactPath,
                                                                            const std::string& metadataPath) {
  using ResultT = atlantis::Result<ValidatedSceneData, SceneArtifactDecodeError>;

  // Step 1: read both files.
  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return ResultT::Err(SceneArtifactDecodeError::ArtifactUnreadable);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return ResultT::Err(SceneArtifactDecodeError::MetadataUnreadable);

  // Steps 2-7: header decode, EmptyScene/NodeCountOutOfRange guards,
  // per-node structural/finite-value decode, cycle re-check,
  // active-camera range/Camera-presence check -- delegated to the
  // shared codec (Step 3's own decodeSceneArtifact()).
  auto decodedArtifactResult = decodeSceneArtifact(artifactBytes);
  if (decodedArtifactResult.isErr()) return ResultT::Err(decodedArtifactResult.error());
  DecodedSceneArtifact& decodedArtifact = decodedArtifactResult.value();

  // Step 8: metadata parse + cross-check against the artifact's own
  // already-validated header fields.
  const auto metadataResult = parseSceneMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(SceneArtifactDecodeError::MetadataParseFailed);
  const SceneMetadata& metadata = metadataResult.value();

  if (metadata.schemaVersion != kSceneArtifactSchemaVersion ||
      metadata.nodeCount != decodedArtifact.nodes.size()) {
    return ResultT::Err(SceneArtifactDecodeError::MetadataArtifactMismatch);
  }

  // Step 9: construct ValidatedSceneData via its own private
  // constructor -- the only call site in the entire codebase permitted
  // to do so, reachable only now that every check above has passed.
  return ResultT::Ok(ValidatedSceneData(std::move(decodedArtifact.nodes), std::move(decodedArtifact.parents),
                                         decodedArtifact.activeCameraIndex));
}

}  // namespace atlantis::asset_system

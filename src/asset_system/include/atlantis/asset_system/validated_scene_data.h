#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/scene_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace atlantis::asset_system {

struct ValidatedSceneNode {
  DecodedTransform transform;
  std::optional<DecodedCamera> camera;
  std::optional<DecodedRenderable> renderable;
};

class ValidatedSceneData;

// Forward-declared here so ValidatedSceneData's own friend declaration
// below can name it -- the real definition lives in decode_scene.h,
// which this header is deliberately not included from (nothing here
// needs the rest of that header's own declarations).
[[nodiscard]] atlantis::Result<ValidatedSceneData, SceneArtifactDecodeError> decodeScene(
    const std::string& artifactPath, const std::string& metadataPath);

// Plan 0015 Section D2 / ADR-0053 (including its own Human Review
// Correction, 2026-08-23): encapsulated so "fully validated" is a
// type-level guarantee, not a caller convention -- reusing the exact
// pattern atlantis::world::EntityId already established. Every
// structural field private; the only non-default constructor private,
// callable only by decodeScene() (a friend relationship, matching
// EntityId's own friend class World) -- the SOLE privileged entry
// point in this entire codebase, with no test-only exception of any
// kind. Public surface is read-only accessors only -- no setter, no
// mutable reference, no mutable iterator. NO public default
// constructor -- deleted, not merely omitted, so a caller's own
// attempt to default-construct one is a named compiler error. A
// zero-node scene is not representable by this type at all;
// decodeScene() itself never succeeds with one (see
// cook_scene.h/decode_scene.h's own EmptyScene condition) -- there is
// no "empty but valid" instance to represent. A prior revision of this
// header briefly added a second, test-only friend
// (ValidatedSceneDataTestAccess) so tests/world/scene_instantiation_tests.cpp
// could construct arbitrary instances directly; a subsequent review
// round removed it as a genuine invariant weakening -- that test file
// now goes through the real, public cookScene() -> decodeScene() path
// exclusively, like every other consumer.
class ValidatedSceneData {
 public:
  ValidatedSceneData() = delete;

  [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
  [[nodiscard]] const ValidatedSceneNode& node(std::size_t index) const noexcept { return nodes_[index]; }
  [[nodiscard]] std::optional<std::size_t> parentOf(std::size_t index) const noexcept { return parents_[index]; }
  [[nodiscard]] std::optional<std::size_t> activeCameraIndex() const noexcept { return activeCameraIndex_; }

  // Copy/move: defaulted -- nothing on the public surface can mutate an
  // instance, so a copy or a moved-from/to pair are each independently
  // valid by construction; no special handling is needed or written.
  ValidatedSceneData(const ValidatedSceneData&) = default;
  ValidatedSceneData(ValidatedSceneData&&) noexcept = default;
  ValidatedSceneData& operator=(const ValidatedSceneData&) = default;
  ValidatedSceneData& operator=(ValidatedSceneData&&) noexcept = default;

 private:
  friend atlantis::Result<ValidatedSceneData, SceneArtifactDecodeError> decodeScene(const std::string&,
                                                                                      const std::string&);

  ValidatedSceneData(std::vector<ValidatedSceneNode> nodes, std::vector<std::optional<std::size_t>> parents,
                      std::optional<std::size_t> activeCameraIndex)
      : nodes_(std::move(nodes)), parents_(std::move(parents)), activeCameraIndex_(activeCameraIndex) {}

  std::vector<ValidatedSceneNode> nodes_;
  std::vector<std::optional<std::size_t>> parents_;
  std::optional<std::size_t> activeCameraIndex_;
};

}  // namespace atlantis::asset_system

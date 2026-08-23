#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/validated_scene_data.h>
#include <atlantis/result.h>

#include <string>

namespace atlantis::asset_system {

// Plan 0015 Section D6: independently re-validates every D4 condition
// from the artifact's own bytes -- never assumes a well-formed cooker
// output. Delegates the header/structural/finite-value/cycle/active-
// camera portion (D6 steps 2-7) to scene_artifact.h's own
// decodeSceneArtifact(), then performs file I/O (step 1) and the
// metadata parse + cross-check (step 8) itself. The ONLY call site in
// the entire codebase permitted to construct ValidatedSceneData
// (enforced by that class's own friend declaration, D2) -- reachable
// only once every check, including the artifact-level EmptyScene
// guarantee, has already passed (step 9). Redeclares the exact
// signature validated_scene_data.h's own friend declaration already
// names -- this header supplies the real definition
// (decode_scene.cpp); validated_scene_data.h is deliberately not
// included from here in the other direction to avoid a needless
// coupling, only forward-declares what its own friend clause needs.
[[nodiscard]] atlantis::Result<ValidatedSceneData, SceneArtifactDecodeError> decodeScene(
    const std::string& artifactPath, const std::string& metadataPath);

}  // namespace atlantis::asset_system

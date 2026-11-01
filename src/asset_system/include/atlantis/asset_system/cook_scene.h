#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::asset_system {

// Plan 0015 Section D4: a hand-bumped constant identifying this
// cooker's own behavior version -- independently versioned from
// kImporterVersion (cook.h), matching that constant's own
// determinism-preserving role and rationale exactly.
inline constexpr std::string_view kSceneCookerVersion = "atlantis-scene-cooker/2";

// Plan 0065 P1 (ADR-0131 D2): a scene cooked in memory -- exactly the bytes
// cookScene() writes to the artifact file and the sidecar text it writes to
// the metadata file.
struct CookedScene {
  std::vector<std::byte> artifactBytes;
  std::string metadataText;
  friend bool operator==(const CookedScene&, const CookedScene&) = default;
};

// Plan 0015 Section D4: reads and validates the authoring source at
// sourceFilePath, remaps node_id/parent/active-camera references to
// dense array indices, encodes the runtime artifact and metadata
// sidecar, and writes both atomically (write-to-temp-then-rename in
// artifactOutputPath's/metadataOutputPath's own directory, identical
// to cookStaticMesh()'s own established pattern) -- a failed cook
// never leaves a partial file and never touches a pre-existing valid
// one. No separate "logical path for self" parameter -- a scene has
// no AssetId of its own, only its Renderable references do (D2).
// Plan 0047 P8: assetGuid is the scene's own persistent identity,
// recorded in its sidecar; it must not be nil.
// Plan 0065 P1: reads the file, cooks it with cookSceneSource(), and writes
// both results atomically.
[[nodiscard]] atlantis::Result<std::monostate, SceneCookError> cookScene(const std::string& sourceFilePath,
                                                                          const AssetGuid& assetGuid,
                                                                          const std::string& artifactOutputPath,
                                                                          const std::string& metadataOutputPath);

// Plan 0065 P1 (ADR-0131 D2): everything cookScene() does between reading the
// source file and writing the two files -- the parse, every check, the
// artifact encode and the sidecar text -- over source text in memory. The
// errors are cookScene()'s, never SourceFileUnreadable or a write failure.
// Spec 0074: after parsing, empty scenes are refused first, then counts above
// kMaxSceneArtifactNodeCount with NodeCountOutOfRange, before derived cook
// validation/allocation. This is not a bound on parsing or document mapping.
// assetGuid must not be nil.
[[nodiscard]] atlantis::Result<CookedScene, SceneCookError> cookSceneSource(std::string_view sourceText,
                                                                             const AssetGuid& assetGuid);

// Plan 0065 P1 (ADR-0131 D2 and its amendment; Spec 0065 R7 and its Correction
// 2026-10-22): cooks an authoring document in memory through the build's own
// text path -- toParsedSceneSource(), serializeSceneSource(), then
// cookSceneSource() -- so the artifact equals the build's cook of the saved text
// byte for byte, and a document is refused for anything the parser or the cooker
// refuses. The mapping's errors fold in: DanglingParentReference ->
// UndeclaredParentReference, DanglingActiveCameraReference ->
// UndeclaredActiveCameraReference, AmbiguousEntityGuid -> DuplicateEntityGuid.
// Once the text cooks, it is mapped back (toAuthoringScene()); unless that equals
// the document, the cook is refused with NotRepresentableInSource rather than
// narrowed. assetGuid must not be nil.
[[nodiscard]] atlantis::Result<CookedScene, SceneCookError> cookSceneToMemory(const scene::AuthoringScene& document,
                                                                               const AssetGuid& assetGuid);

}  // namespace atlantis::asset_system

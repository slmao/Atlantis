#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <string>
#include <variant>

namespace atlantis::asset_system {

// Plan 0018 Section P4 (signature corrected during Implementation --
// see Milestone 4's own commit message): reads and parses the material
// source file itself (mirroring cookScene()'s own "reads the source
// file itself" shape -- Material's own authoring source is plain text,
// not pre-decoded binary pixels, unlike cookTexture()). Unlike
// cookScene() (which takes no logicalPathInput, since a scene has no
// AssetId of its own), Material IS its own fourth Asset System asset
// type with its own AssetId (Spec 0018 D1) -- this function therefore
// takes logicalPathInput exactly like cookStaticMesh()/cookTexture() do,
// for this material's OWN identity, independent of the texture logical
// path its own source file names.
//
// Steps: read + parseMaterialSource() (-> SourceParseFailed); normalize
// THIS material's own logicalPathInput via normalizeLogicalPath() (->
// LogicalPathInvalid, matching cookStaticMesh()'s/cookTexture()'s own
// precedent) and computeAssetId() on it for the metadata sidecar's own
// assetId; separately normalize the parsed texture logical path (->
// LogicalPathInvalid also) and computeAssetId() on it for the artifact's
// own embedded texture_asset_id -- never an existence check on either
// (ADR-0059 D6/D7); encode + atomic write (temp-then-rename()).
[[nodiscard]] atlantis::Result<std::monostate, MaterialCookError> cookMaterial(const std::string& sourceFilePath,
                                                                                const std::string& logicalPathInput,
                                                                                const std::string& artifactOutputPath,
                                                                                const std::string& metadataOutputPath);

}  // namespace atlantis::asset_system

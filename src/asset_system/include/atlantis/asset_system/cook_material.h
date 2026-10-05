#pragma once

#include <atlantis/asset_system/asset_guid.h>
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
// precedent), recorded as provenance; each parsed texture reference is a
// GUID whose assetKey() is the artifact's embedded texture_asset_id --
// never an existence check on it (ADR-0059 D6/D7); encode + atomic write
// (temp-then-rename()).
// Plan 0047 P7 (ADR-0097 D1/D2): assetGuid is the asset's persistent
// identity, resolved by the caller (the cooker, from the catalog source or
// a cook-manifest --guid=); it must not be nil. The Asset ID written into
// the artifact and sidecar is assetKey(assetGuid); logicalPathInput is
// normalized and recorded as provenance only.
[[nodiscard]] atlantis::Result<std::monostate, MaterialCookError> cookMaterial(const std::string& sourceFilePath,
                                                                                const std::string& logicalPathInput,
                                                                                const AssetGuid& assetGuid,
                                                                                const std::string& artifactOutputPath,
                                                                                const std::string& metadataOutputPath);

}  // namespace atlantis::asset_system

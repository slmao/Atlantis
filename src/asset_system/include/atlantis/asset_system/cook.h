#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0012 Section D5: a hand-bumped constant identifying this
// importer's own behavior version -- not a git revision, which would
// change the metadata sidecar's bytes on every unrelated commit and
// defeat this Spec's own determinism verification (see
// cooker_determinism_tests.cpp). Bumped by hand whenever import logic
// changes in a way that could alter output bytes.
inline constexpr std::string_view kImporterVersion = "atlantis-asset-cooker/1";

// Plan 0012 Step 4 / ADR-0043: reads the authoring source at
// sourceFilePath, normalizes and validates logicalPathInput (Plan 0012
// Section D9 -- this function is the sole point that normalizes it;
// callers pass the raw, not-yet-normalized path), computes its Asset
// ID, encodes the runtime artifact and metadata sidecar, and writes
// both atomically (Plan 0012 Section D10: write-to-temp-then-rename()
// in the same directory as artifactOutputPath/metadataOutputPath) -- a
// failed cook never leaves a partial file and never touches a
// pre-existing valid one.
// Plan 0047 P7 (ADR-0097 D1/D2): assetGuid is the asset's persistent
// identity, resolved by the caller (the cooker, from the catalog source or
// a cook-manifest --guid=); it must not be nil. The Asset ID written into
// the artifact and sidecar is assetKey(assetGuid); logicalPathInput is
// normalized and recorded as provenance only.
[[nodiscard]] atlantis::Result<std::monostate, CookError> cookStaticMesh(const std::string& sourceFilePath,
                                                                          const std::string& logicalPathInput,
                                                                          const AssetGuid& assetGuid,
                                                                          const std::string& artifactOutputPath,
                                                                          const std::string& metadataOutputPath);

}  // namespace atlantis::asset_system

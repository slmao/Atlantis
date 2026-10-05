#pragma once

#include <string>

struct AAssetManager;

namespace atlantis::runtime::android_detail {

// ADR-0080's own asset-delivery decision: extracts one packaged asset
// (relativePath, e.g. "shaders/minimal_renderer/minimal_mesh.vert.spv")
// from mgr into <internalDataPath>/<relativePath>, creating intermediate
// directories as needed. Skips the actual copy if the destination
// already exists with the exact same byte size as the packaged asset
// (ADR-0080's own "skip re-copying on a subsequent run" rule) -- a
// cheap, sufficient staleness check for this Spec's scope, not a
// hash/timestamp check. Returns the destination's absolute path on
// success, or an empty string if the asset could not be opened, read,
// or written (logged via ATLANTIS_LOG_ERROR at the point of failure).
[[nodiscard]] std::string extractAsset(AAssetManager* mgr, const std::string& internalDataPath,
                                        const std::string& relativePath);

// Plan 0047 P19 (ADR-0098 D3): extracts the packaged closure catalog
// (relativePath, e.g. "integrated_showcase_demo.catalog.txt") and every
// artifact and metadata file it lists, each at its catalog-relative location
// under <internalDataPath>, so the catalog's own relative locations resolve
// unchanged on the device -- no path rewriting. Parses the catalog with the
// Asset System's own parser (parseAssetCatalogRecords()); the catalog file
// itself is rewritten on every call (it is a few KB of text), the listed
// files follow extractAsset()'s skip-if-unchanged rule. Returns the
// extracted catalog's absolute path on success, or an empty string on any
// failure (logged at the point of failure).
[[nodiscard]] std::string extractCatalogClosure(AAssetManager* mgr, const std::string& internalDataPath,
                                                 const std::string& relativePath);

}  // namespace atlantis::runtime::android_detail

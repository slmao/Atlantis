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

// ADR-0080's own asset-delivery decision, extended by one necessary
// wrinkle found while implementing it (Plan 0034 Milestone 5): a
// scene's own dependency manifest (scene_manifest.h's tab-separated
// <logical path>\t<artifact path>\t<metadata path> format) is generated
// at Windows-build time with absolute Windows build-tree paths baked
// into its artifact/metadata columns -- meaningless on Android.
// Gradle's own packaging task (android/app/build.gradle) already
// rewrites those two columns to paths relative to the packaged assets
// root before copying the manifest into the APK; this function performs
// the second half of that rewrite, the half only runtime code can do
// (it needs the real, per-device internalDataPath Gradle cannot know
// ahead of time): extracts every dependency file the manifest's
// now-relative columns reference (via extractAsset() above), rewrites
// those same columns to each extraction's own real absolute path, and
// writes the result to <internalDataPath>/<relativePath> --
// regenerated unconditionally on every call (this file is a few KB of
// text, not one of the bulky binary assets extractAsset()'s own
// skip-if-unchanged check exists for, so always rewriting it costs
// nothing worth special-casing). Returns the destination's absolute
// path on success, or an empty string on any failure.
[[nodiscard]] std::string extractSceneManifest(AAssetManager* mgr, const std::string& internalDataPath,
                                                const std::string& relativePath);

}  // namespace atlantis::runtime::android_detail

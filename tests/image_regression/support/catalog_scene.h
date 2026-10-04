#pragma once

// Plan 0047 M5 (P15): the GPU/image-regression tests select their scene by
// catalog GUID. The ATLANTIS_*_GUID compile definitions are generated from
// assets/asset_catalog.txt at configure time, so a malformed value is a
// build-system defect, not test input.

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_guid.h>

#include <string>

namespace atlantis::image_regression {

[[nodiscard]] inline atlantis::asset_system::AssetGuid sceneGuidFromDefinition(const char* text) {
  auto parsed = atlantis::asset_system::parseAssetGuid(text);
  ATLANTIS_CHECK_MSG(parsed.isOk(), "a generated ATLANTIS_*_GUID compile definition is not a valid GUID");
  return parsed.value();
}

// The imported Bistro scene's GUID text: derived from its import root's
// catalog GUID (ADR-0097 D4, Plan 0047 P15).
[[nodiscard]] inline std::string derivedSceneGuidText(const char* importRootGuidText) {
  return atlantis::asset_system::toString(
      atlantis::asset_system::deriveAssetGuid(sceneGuidFromDefinition(importRootGuidText), "scene"));
}

}  // namespace atlantis::image_regression

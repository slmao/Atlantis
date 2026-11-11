#pragma once

#include "../support/pixel_diff.h"

#include <optional>
#include <string_view>

namespace atlantis::image_regression {
inline constexpr std::string_view kPreviewCubeGuid = "3b9b8177-eac1-4139-b203-a49d5577eb3b";
inline constexpr std::string_view kPreviewQuadGuid = "58e57a64-ad60-4925-b6d6-6561540e8476";
inline constexpr std::string_view kMeshPreviewGolden = "mesh_preview/mesh_preview_256x256_rgba8unorm";

// Cooked catalog input and the actual Runtime preview helper, on a headless
// validation device. The generator and read-only comparator share this recipe.
[[nodiscard]] std::optional<PixelBuffer> renderMeshPreviewPixels(std::string_view guid);
}  // namespace atlantis::image_regression

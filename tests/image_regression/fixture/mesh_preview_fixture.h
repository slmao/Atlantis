#pragma once

#include "../support/pixel_diff.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace atlantis::image_regression {
inline constexpr std::string_view kPreviewCubeGuid = "3b9b8177-eac1-4139-b203-a49d5577eb3b";
inline constexpr std::string_view kPreviewQuadGuid = "58e57a64-ad60-4925-b6d6-6561540e8476";
inline constexpr std::string_view kMeshPreviewGolden = "mesh_preview/mesh_preview_256x256_rgba8unorm";
inline constexpr std::string_view kPairPreviewGolden = "mesh_material_preview/mesh_material_preview_256x256_rgba8unorm";
inline constexpr std::string_view kPreviewDielectricGuid = "1542b95d-74b5-48ba-82be-eddbe2843129";
inline constexpr std::string_view kPreviewStainedWoodGuid = "057d2d26-8bb6-4a8a-a05a-39ec65a84857";
inline constexpr std::string_view kPreviewAshWoodGuid = "ce676321-102e-4f55-91c4-dd36465721d7";

// Cooked catalog input and the actual Runtime preview helper, on a headless
// validation device. The generator and read-only comparator share this recipe.
[[nodiscard]] std::optional<PixelBuffer> renderMeshPreviewPixels(std::string_view guid);
[[nodiscard]] std::optional<PixelBuffer> renderMeshMaterialPreviewPixels(std::string_view materialGuid,
                                                                        bool multiMipFixture = false);
[[nodiscard]] std::optional<PixelBuffer> renderPbrMapPreviewPixels(
    bool normal, bool emissive, float emission, float metallic = 0.0f, float roughness = 0.9f,
    std::optional<std::array<std::uint8_t, 4>> packed = std::nullopt);
}  // namespace atlantis::image_regression

#include "mesh_preview_fixture.h"

#include "../../../src/runtime/src/mesh_preview.h"
#include "../support/png_codec.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/load_material.h>
#include <atlantis/asset_system/load_texture.h>
#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <memory>
#include <utility>

namespace atlantis::image_regression {
namespace {
runtime::BootstrapConfig previewConfig() {
  runtime::BootstrapConfig config;
  config.vertexShaderSpirvPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.vert.spv";
  config.vertexShaderReflectionPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.vert.refl.json";
  config.fragmentShaderSpirvPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.frag.spv";
  config.fragmentShaderReflectionPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath = ATLANTIS_PREVIEW_PBR_DIR "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = ATLANTIS_PREVIEW_PBR_DIR "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = ATLANTIS_PREVIEW_PBR_DIR "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitNormalMapVertexShaderSpirvPath = ATLANTIS_PREVIEW_NORMAL_DIR "/pbr_direct_lit_normal_map.vert.spv";
  config.pbrDirectLitNormalMapVertexShaderReflectionPath = ATLANTIS_PREVIEW_NORMAL_DIR "/pbr_direct_lit_normal_map.vert.refl.json";
  config.pbrDirectLitNormalMapFragmentShaderSpirvPath = ATLANTIS_PREVIEW_NORMAL_DIR "/pbr_direct_lit_normal_map.frag.spv";
  config.pbrDirectLitMetallicRoughnessFragmentShaderSpirvPath =
      ATLANTIS_PREVIEW_PACKED_DIR "/pbr_direct_lit_metallic_roughness.frag.spv";
  config.pbrDirectLitNormalMapMetallicRoughnessFragmentShaderSpirvPath =
      ATLANTIS_PREVIEW_NORMAL_PACKED_DIR "/pbr_direct_lit_normal_map_metallic_roughness.frag.spv";
  config.shadowCastVertexShaderSpirvPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.frag.refl.json";

  return config;
}

template <class Render>
std::optional<PixelBuffer> renderPreviewPixels(Render render) {
  auto device = vulkan_backend::createDevice({.applicationName = "Atlantis Mesh Preview Fixture", .enableValidationLayers = true});
  if (device.isErr()) return std::nullopt;
  std::shared_ptr<runtime::detail::MeshPreviewResources> incomplete;
  auto execution = render(*device.value(), previewConfig(), incomplete);
  if (incomplete && device.value()->waitIdle().isErr()) ATLANTIS_LOG_ERROR("Mesh preview fixture: shutdown drain failed");
  incomplete.reset();
  if (execution.result.isErr() || execution.fatal) return std::nullopt;
  auto png = decodePng(execution.result.value().png);
  if (png.isErr()) return std::nullopt;
  return std::move(png.value().pixels);
}
}  // namespace

std::optional<PixelBuffer> renderMeshPreviewPixels(std::string_view guidText) {
  auto guid = asset_system::parseAssetGuid(guidText);
  auto catalog = asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  if (guid.isErr() || catalog.isErr()) return std::nullopt;
  const auto* record = catalog.value().find(guid.value());
  if (!record || record->type != asset_system::CatalogAssetType::Mesh) return std::nullopt;
  auto mesh = asset_system::loadStaticMeshPreviewAsset(*record);
  if (mesh.isErr()) return std::nullopt;
  return renderPreviewPixels([&](auto& device, const auto& config, auto& incomplete) {
    return runtime::detail::renderMeshPreview(device, config, mesh.value(), guid.value(), incomplete);
  });
}

std::optional<PixelBuffer> renderMeshMaterialPreviewPixels(std::string_view materialGuidText, bool multiMipFixture) {
  const auto meshGuid = asset_system::parseAssetGuid(kPreviewQuadGuid);
  const auto materialGuid = asset_system::parseAssetGuid(materialGuidText);
  auto catalog = asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  if (meshGuid.isErr() || materialGuid.isErr() || catalog.isErr()) return std::nullopt;
  const auto* meshRecord = catalog.value().find(meshGuid.value());
  const auto* materialRecord = catalog.value().find(materialGuid.value());
  if (!meshRecord || !materialRecord) return std::nullopt;
  auto mesh = asset_system::loadStaticMeshPreviewAsset(*meshRecord);
  auto material = asset_system::loadPreviewMaterialAsset(*materialRecord);
  if (mesh.isErr() || material.isErr()) return std::nullopt;
  const auto* textureRecord = catalog.value().find(material.value().textureAsset);
  if (!textureRecord) return std::nullopt;
  auto texture = asset_system::loadPreviewBaseColorTexture(*textureRecord);
  if (texture.isErr()) return std::nullopt;

  if (multiMipFixture) {
    // The quad is minified at 256 output pixels. Red exists only at
    // level zero, so green verifies upload and sampling of level one.
    auto& data = texture.value();
    data.width = data.height = 512;
    data.mipCount = 2;
    constexpr std::size_t levelZeroBytes = 512 * 512 * 4;
    data.pixelBytes.assign(levelZeroBytes + 256 * 256 * 4, 0);
    for (std::size_t i = 0; i < data.pixelBytes.size(); i += 4) {
      data.pixelBytes[i + (i < levelZeroBytes ? 0 : 1)] = 255;
      data.pixelBytes[i + 3] = 255;
    }
  }
  return renderPreviewPixels([&](auto& device, const auto& config, auto& incomplete) {
    return runtime::detail::renderMeshMaterialPreview(device, config, mesh.value(), material.value(), texture.value(),
                                                      meshGuid.value(), materialGuid.value(), incomplete);
  });
}
std::optional<PixelBuffer> renderPbrMapPreviewPixels(
    bool normal, bool emissive, float emission, float metallic, float roughness,
    std::optional<std::array<std::uint8_t, 4>> packed) {
  namespace as = asset_system;
  const auto meshId = as::parseAssetGuid(kPreviewQuadGuid).value();
  const auto materialId = as::parseAssetGuid(kPreviewDielectricGuid).value();
  const auto normalId = as::parseAssetGuid("5e99757a-d524-4aaf-b665-0d308aa7ae99").value();
  auto catalog = as::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  if (catalog.isErr()) return {};
  const auto* meshRecord = catalog.value().find(meshId);
  const auto* materialRecord = catalog.value().find(materialId);
  const auto* normalRecord = catalog.value().find(normalId);
  if (!meshRecord || !materialRecord || !normalRecord) return {};
  auto mesh = as::loadStaticMeshPreviewAsset(*meshRecord);
  auto material = as::loadPreviewMaterialAsset(*materialRecord);
  if (mesh.isErr() || material.isErr()) return {};
  const auto* baseRecord = catalog.value().find(material.value().textureAsset);
  if (!baseRecord) return {};
  auto base = as::loadPreviewBaseColorTexture(*baseRecord);
  auto normalTexture = as::loadPreviewBaseColorTexture(*normalRecord);
  if (base.isErr() || normalTexture.isErr()) return {};
  auto lightMask = base.value();
  lightMask.width = lightMask.height = lightMask.mipCount = 1;
  lightMask.pixelBytes.assign({128, 64, 32, 255});
  auto& recipe = material.value();
  recipe.normalMapTexture = normal ? normalRecord->assetId : 0;
  recipe.emissiveTexture = emissive ? as::assetKey(materialId) : 0;
  recipe.emissiveFactor[0] = emission;
  recipe.emissiveFactor[1] = emission;
  recipe.emissiveFactor[2] = emission;
  recipe.metallicFactor = metallic;
  recipe.roughnessFactor = roughness;
  as::TextureAssetData packedTexture;
  if (packed) {
    recipe.metallicRoughnessMap = as::assetKey(materialId) + 1;
    packedTexture.width = packedTexture.height = packedTexture.mipCount = 1;
    packedTexture.pixelBytes.assign(packed->begin(), packed->end());
  }
  return renderPreviewPixels([&](auto& device, const auto& config, auto& incomplete) {
    return runtime::detail::renderMeshMaterialPreview(device, config, mesh.value(), recipe, base.value(),
        meshId, materialId, incomplete, nullptr, nullptr, normal ? &normalTexture.value() : nullptr,
        emissive ? &lightMask : nullptr, packed ? &packedTexture : nullptr);
  });
}
}  // namespace atlantis::image_regression

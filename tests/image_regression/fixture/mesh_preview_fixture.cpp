#include "mesh_preview_fixture.h"

#include "../../../src/runtime/src/mesh_preview.h"
#include "../support/png_codec.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <memory>
#include <utility>

namespace atlantis::image_regression {
std::optional<PixelBuffer> renderMeshPreviewPixels(std::string_view guidText) {
  auto guid = asset_system::parseAssetGuid(guidText);
  auto catalog = asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  if (guid.isErr() || catalog.isErr()) return std::nullopt;
  const auto* record = catalog.value().find(guid.value());
  if (!record || record->type != asset_system::CatalogAssetType::Mesh) return std::nullopt;
  auto mesh = asset_system::loadStaticMeshPreviewAsset(*record);
  if (mesh.isErr()) return std::nullopt;

  runtime::BootstrapConfig config;
  config.vertexShaderSpirvPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.vert.spv";
  config.vertexShaderReflectionPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.vert.refl.json";
  config.fragmentShaderSpirvPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.frag.spv";
  config.fragmentShaderReflectionPath = ATLANTIS_PREVIEW_MINIMAL_DIR "/minimal_mesh.frag.refl.json";
  config.shadowCastVertexShaderSpirvPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = ATLANTIS_PREVIEW_SHADOW_DIR "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = ATLANTIS_PREVIEW_OUTPUT_DIR "/output_transform_unorm.frag.refl.json";

  auto device = vulkan_backend::createDevice({.applicationName = "Atlantis Mesh Preview Fixture", .enableValidationLayers = true});
  if (device.isErr()) return std::nullopt;
  std::shared_ptr<runtime::detail::MeshPreviewResources> incomplete;
  auto execution = runtime::detail::renderMeshPreview(*device.value(), config, mesh.value(), guid.value(), incomplete);
  if (incomplete && device.value()->waitIdle().isErr()) ATLANTIS_LOG_ERROR("Mesh preview fixture: shutdown drain failed");
  incomplete.reset();
  if (execution.result.isErr() || execution.fatal) return std::nullopt;
  auto png = decodePng(execution.result.value().png);
  if (png.isErr()) return std::nullopt;
  return std::move(png.value().pixels);
}
}  // namespace atlantis::image_regression

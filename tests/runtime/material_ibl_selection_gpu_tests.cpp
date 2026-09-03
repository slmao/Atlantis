#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/renderer/material.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

std::optional<std::vector<std::uint32_t>> loadSpirv(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;
  const std::streamsize bytes = file.tellg();
  if (bytes <= 0 || bytes % 4 != 0) return std::nullopt;
  file.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes) / 4);
  if (!file.read(reinterpret_cast<char*>(words.data()), bytes)) return std::nullopt;
  return words;
}

}  // namespace

TEST_CASE("PBR material realization selects the IBL pipeline only when an environment is enabled",
          "[runtime][gpu][material_realization][ibl]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis IBL Material Selection Tests", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  auto device = std::move(deviceResult.value());

  const auto directVertex =
      loadSpirv(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv");
  const auto directFragment =
      loadSpirv(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv");
  const auto iblVertex = loadSpirv(std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv");
  const auto iblFragment = loadSpirv(std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv");
  REQUIRE(directVertex.has_value());
  REQUIRE(directFragment.has_value());
  REQUIRE(iblVertex.has_value());
  REQUIRE(iblFragment.has_value());

  const atlantis::rhi::VertexInputLayout layout{
      .strideBytes = 44,
      .attributes = {{.location = 0, .offsetBytes = 0, .format = atlantis::rhi::VertexAttributeFormat::Float3},
                     {.location = 1, .offsetBytes = 24, .format = atlantis::rhi::VertexAttributeFormat::Float2},
                     {.location = 2, .offsetBytes = 32, .format = atlantis::rhi::VertexAttributeFormat::Float3}}};
  const atlantis::asset_system::MaterialAssetData materialData{
      .kind = atlantis::asset_system::MaterialKind::PbrDirectLit, .textureAsset = 7};
  atlantis::asset_system::TextureAssetData textureData;
  textureData.width = 1;
  textureData.height = 1;
  textureData.colorSpace = atlantis::asset_system::TextureColorSpace::Srgb;
  textureData.pixelBytes = {255, 255, 255, 255};
  const std::unordered_map<atlantis::asset_system::AssetId, const atlantis::rhi::SampledTexture*> noTextures;

  auto direct = atlantis::runtime::realizeOneMaterialCandidate(
      *device, layout, *directVertex, *directFragment, layout, *directVertex, *directFragment, layout,
      *directVertex, *directFragment, layout, *iblVertex, *iblFragment, false, 1, materialData, textureData,
      noTextures);
  REQUIRE(direct.isOk());
  CHECK(direct.value().material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::None);

  auto ibl = atlantis::runtime::realizeOneMaterialCandidate(
      *device, layout, *directVertex, *directFragment, layout, *directVertex, *directFragment, layout,
      *directVertex, *directFragment, layout, *iblVertex, *iblFragment, true, 2, materialData, textureData,
      noTextures);
  REQUIRE(ibl.isOk());
  CHECK(ibl.value().material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  REQUIRE(device->waitIdle().isOk());
}

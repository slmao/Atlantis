#pragma once

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/load_material.h>
#include <atlantis/asset_system/load_texture.h>
#include <atlantis/asset_system/material_source.h>
#include <atlantis/connection/asset_connection.h>

#include <algorithm>

namespace atlantis::runtime::material_authoring {
namespace as = asset_system;
namespace ca = connection::assets;

inline Result<as::TextureAssetData, ca::MaterialError> texture(
    const as::AssetCatalog& catalog, const as::AssetGuid& guid) {
  using E = ca::MaterialError;
  using R = Result<as::TextureAssetData, E>;
  const auto* record = catalog.find(guid);
  if (!record || record->type != as::CatalogAssetType::Texture || !record->dependencies.empty())
    return R::Err(E::DependencyUnavailable);
  auto loaded = as::loadPreviewBaseColorTexture(*record);
  if (loaded.isErr())
    return R::Err(loaded.error() == as::PreviewArtifactLoadError::TooLarge ? E::TooLarge : E::DependencyUnavailable);
  if (loaded.value().colorSpace != as::TextureColorSpace::Srgb ||
      loaded.value().layout != as::TextureDataLayout::Rgba8)
    return R::Err(E::UnsupportedMaterial);
  return R::Ok(std::move(loaded.value()));
}

inline Result<ca::MaterialRecipe, ca::MaterialError> read(
    const as::AssetCatalog& catalog, const as::AssetGuid& guid) {
  using E = ca::MaterialError;
  using R = Result<ca::MaterialRecipe, E>;
  const auto* record = catalog.find(guid);
  if (!record) return R::Err(E::UnknownMaterial);
  if (record->type != as::CatalogAssetType::Material) return R::Err(E::NotMaterial);
  const auto loaded = as::loadPreviewMaterialAsset(*record);
  if (loaded.isErr())
    return R::Err(loaded.error() == as::PreviewArtifactLoadError::TooLarge ? E::TooLarge : E::InvalidMaterial);
  const auto& mat = loaded.value();
  if (mat.kind != as::MaterialKind::PbrDirectLit || mat.alphaMode != as::MaterialAlphaMode::Opaque ||
      mat.baseColorFactor[3] != 1.0f || mat.alphaCutoff != 0.5f || mat.normalMapTexture || mat.emissiveTexture ||
      mat.emissiveFactor[0] != 0.0f || mat.emissiveFactor[1] != 0.0f || mat.emissiveFactor[2] != 0.0f)
    return R::Err(E::UnsupportedMaterial);
  const auto* dep = catalog.find(mat.textureAsset);
  if (!dep || record->dependencies.size() != 1 || record->dependencies.front() != dep->guid)
    return R::Err(E::DependencyUnavailable);
  auto base = texture(catalog, dep->guid);
  if (base.isErr()) return R::Err(base.error());
  const ca::MaterialRecipe recipe{{dep->guid, {mat.baseColorFactor[0], mat.baseColorFactor[1],
      mat.baseColorFactor[2]}, mat.metallicFactor, mat.roughnessFactor},
      mat.filter == as::MaterialSamplerFilter::Linear ? ca::MaterialFilter::Linear : ca::MaterialFilter::Nearest,
      mat.addressMode == as::MaterialSamplerAddressMode::Repeat ? ca::MaterialAddress::Repeat
                                                              : ca::MaterialAddress::ClampToEdge};
  return ca::validMaterialRecipe(recipe) ? R::Ok(recipe) : R::Err(E::InvalidMaterial);
}

inline as::ParsedMaterialSource source(const ca::MaterialRecipe& recipe) {
  as::ParsedMaterialSource out;
  out.kind = as::MaterialKind::PbrDirectLit;
  out.textureAsset = recipe.values.texture;
  out.filter = recipe.filter == ca::MaterialFilter::Linear ? as::MaterialSamplerFilter::Linear
                                                           : as::MaterialSamplerFilter::Nearest;
  out.addressMode = recipe.address == ca::MaterialAddress::Repeat ? as::MaterialSamplerAddressMode::Repeat
                                                                : as::MaterialSamplerAddressMode::ClampToEdge;
  std::copy(recipe.values.color.begin(), recipe.values.color.end(), out.baseColorFactor);
  out.metallicFactor = recipe.values.metallic;
  out.roughnessFactor = recipe.values.roughness;
  return out;
}

inline bool matchesFrozen(const as::AssetCatalog& catalog, const as::AssetGuid& guid, std::string_view bytes) {
  const auto parsed = as::parseMaterialSource(bytes);
  const auto cooked = read(catalog, guid);
  return parsed.isOk() && cooked.isOk() &&
      as::serializeMaterialSource(parsed.value()) == as::serializeMaterialSource(source(cooked.value()));
}

inline as::MaterialAssetData previewData(const ca::MaterialRecipe& recipe) {
  const auto input = source(recipe);
  as::MaterialAssetData out;
  out.kind = input.kind;
  out.textureAsset = as::assetKey(input.textureAsset);
  out.filter = input.filter;
  out.addressMode = input.addressMode;
  std::copy(recipe.values.color.begin(), recipe.values.color.end(), out.baseColorFactor);
  out.metallicFactor = input.metallicFactor;
  out.roughnessFactor = input.roughnessFactor;
  return out;
}
}  // namespace atlantis::runtime::material_authoring

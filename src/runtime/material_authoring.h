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
    const as::AssetCatalog& catalog, const as::AssetGuid& guid,
    as::TextureColorSpace color = as::TextureColorSpace::Srgb) {
  using E = ca::MaterialError;
  using R = Result<as::TextureAssetData, E>;
  const auto* record = catalog.find(guid);
  if (!record || record->type != as::CatalogAssetType::Texture || !record->dependencies.empty())
    return R::Err(E::DependencyUnavailable);
  auto loaded = as::loadPreviewBaseColorTexture(*record);
  if (loaded.isErr())
    return R::Err(loaded.error() == as::PreviewArtifactLoadError::TooLarge ? E::TooLarge : E::DependencyUnavailable);
  if (loaded.value().colorSpace != color ||
      loaded.value().layout != as::TextureDataLayout::Rgba8)
    return R::Err(E::UnsupportedMaterial);
  return R::Ok(std::move(loaded.value()));
}

struct RecipeTextures {
  as::TextureAssetData base;
  std::optional<as::TextureAssetData> normal;
  std::optional<as::TextureAssetData> emissive;
};

inline Result<RecipeTextures, ca::MaterialError> textures(
    const as::AssetCatalog& catalog, const ca::MaterialValues& values, std::string_view* slot = nullptr) {
  using E = ca::MaterialError;
  using R = Result<RecipeTextures, E>;
  if (slot) *slot = "Base color";
  auto base = texture(catalog, values.texture);
  if (base.isErr()) return R::Err(base.error());
  RecipeTextures result{std::move(base.value()), {}, {}};
  if (values.normalMap) {
    if (slot) *slot = "Normal map";
    auto normal = texture(catalog, *values.normalMap, as::TextureColorSpace::Unorm);
    if (normal.isErr()) return R::Err(normal.error());
    result.normal = std::move(normal.value());
  }
  if (values.emissiveTexture && *values.emissiveTexture != values.texture) {
    if (slot) *slot = "Emissive texture";
    auto emissive = texture(catalog, *values.emissiveTexture);
    if (emissive.isErr()) return R::Err(emissive.error());
    result.emissive = std::move(emissive.value());
  }
  return R::Ok(std::move(result));
}

inline std::optional<ca::MaterialValues> cookedValues(
    const as::AssetCatalog& catalog, const as::AssetCatalogRecord& record, const as::MaterialAssetData& mat) {
  const auto* base = catalog.find(mat.textureAsset);
  const auto* normal = mat.normalMapTexture ? catalog.find(mat.normalMapTexture) : nullptr;
  const auto* emissive = mat.emissiveTexture ? catalog.find(mat.emissiveTexture) : nullptr;
  if (!base || (mat.normalMapTexture && !normal) || (mat.emissiveTexture && !emissive)) return {};
  std::vector<as::AssetGuid> dependencies{base->guid};
  if (normal) dependencies.push_back(normal->guid);
  if (emissive && emissive->guid != base->guid) dependencies.push_back(emissive->guid);
  if (!std::ranges::is_permutation(record.dependencies, dependencies)) return {};
  return ca::MaterialValues{base->guid, {mat.baseColorFactor[0], mat.baseColorFactor[1], mat.baseColorFactor[2]},
      mat.metallicFactor, mat.roughnessFactor, normal ? std::optional{normal->guid} : std::nullopt,
      emissive ? std::optional{emissive->guid} : std::nullopt,
      {mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]}};
}

inline Result<ca::MaterialRecipe, ca::MaterialError> read(
    const as::AssetCatalog& catalog, const as::AssetGuid& guid,
    ca::MaterialRecipeVersion version = ca::MaterialRecipeVersion::WithMaps) {
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
      mat.baseColorFactor[3] != 1.0f || mat.alphaCutoff != 0.5f)
    return R::Err(E::UnsupportedMaterial);
  if (version == ca::MaterialRecipeVersion::Basic &&
      (mat.normalMapTexture || mat.emissiveTexture ||
       std::ranges::any_of(mat.emissiveFactor, [](float value) { return value != 0.0f; })))
    return R::Err(E::UnsupportedMaterial);
  const auto values = cookedValues(catalog, *record, mat);
  if (!values) return R::Err(E::DependencyUnavailable);
  if (auto loadedTextures = textures(catalog, *values); loadedTextures.isErr()) return R::Err(loadedTextures.error());
  const ca::MaterialRecipe recipe{*values,
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
  out.normalMapAsset = recipe.values.normalMap;
  out.emissiveTextureAsset = recipe.values.emissiveTexture;
  std::ranges::copy(recipe.values.emissiveFactor, out.emissiveFactor);
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
  if (input.normalMapAsset) out.normalMapTexture = as::assetKey(*input.normalMapAsset);
  if (input.emissiveTextureAsset) out.emissiveTexture = as::assetKey(*input.emissiveTextureAsset);
  std::ranges::copy(recipe.values.emissiveFactor, out.emissiveFactor);
  return out;
}
}  // namespace atlantis::runtime::material_authoring

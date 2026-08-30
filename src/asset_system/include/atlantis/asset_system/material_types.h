#pragma once

#include <atlantis/asset_system/asset_id.h>

namespace atlantis::asset_system {

// Plan 0018 Section P1 / ADR-0059 Decision items 1/2: Asset System's own,
// independent sampler-parameter enums -- deliberately never
// atlantis::rhi::Filter/AddressMode, matching TextureColorSpace's own
// established precedent of never naming an RHI type. A composition root
// outside Asset System (Runtime, Milestone 12) is the only place that
// translates these into atlantis::rhi::SamplerCreateParams.
enum class MaterialSamplerFilter {
  Nearest,
  Linear,
};

enum class MaterialSamplerAddressMode {
  Repeat,
  ClampToEdge,
};

// A small, closed enum. Runtime maps each value to a fixed, built-in
// shader pair (Plan 0018 Section P10, Plan 0019 Section P6); the
// Material artifact stores only this enumerator, never a shader path or
// identifier of any kind. LitTextured added by ADR-0061 Decision 3 /
// plans/0019-lighting-foundation.md P5 -- reuses MaterialAssetData's own
// existing, unchanged shape; no LitColored (untextured) kind this
// round, no real consumer names one yet. PbrDirectLit added by ADR-0066
// Decision item 1 / Plan 0023 Milestone 1 -- reuses this same shape too,
// widened by three new fields below (present, but inert, for the other
// two kinds).
enum class MaterialKind {
  UnlitTextured,
  LitTextured,
  PbrDirectLit,
};

// CPU-side result of loadMaterialAsset() -- names no RHI type, matching
// TextureAssetData's own discipline exactly. A composition root outside
// Asset System is responsible for resolving textureAsset to real pixel
// data (via loadTextureAsset()) and constructing any RHI Sampler/
// SampledTexture/Material from these fields.
//
// baseColorFactor/metallicFactor/roughnessFactor (ADR-0066 Decision item
// 1): present on every MaterialKind via one unconditional schema, only
// PbrDirectLit gives them real rendering meaning -- UnlitTextured/
// LitTextured never read them. baseColorFactor is authored and consumed
// in linear space (ADR-0066 item 7). Defaults are inert placeholders,
// never real content for a non-PbrDirectLit asset.
struct MaterialAssetData {
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
};

}  // namespace atlantis::asset_system

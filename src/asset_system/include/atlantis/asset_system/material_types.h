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
// docs/plans/0019-lighting-foundation.md P5 -- reuses MaterialAssetData's own
// existing, unchanged shape; no LitColored (untextured) kind this
// round, no real consumer names one yet. PbrDirectLit added by ADR-0066
// Decision item 1 / Plan 0023 Milestone 1 -- reuses this same shape too,
// widened by three new fields below (present, but inert, for the other
// two kinds).
// Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat added -- its own new,
// mutually-exclusive kind, not a feature flag on PbrDirectLit (ADR-0081's
// own Decision). Reuses this same MaterialAssetData shape, widened by
// two new fields below (present, but inert, for every other kind).
enum class MaterialKind {
  UnlitTextured,
  LitTextured,
  PbrDirectLit,
  PbrClearcoat,
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
  // Plan 0029 Section P5/P6/ADR-0074 Section 1: `0` = none, the same
  // "unassigned" convention `Renderable::meshAsset`'s own default
  // already establishes. Legal (non-zero) only when kind ==
  // PbrDirectLit or PbrClearcoat -- enforced at parse time
  // (material_source.cpp), never here.
  AssetId normalMapTexture = 0;
  // Plan 0035 Milestone 2 (ADR-0081): present on every MaterialKind via
  // this one unconditional schema (mirroring baseColorFactor/
  // metallicFactor/roughnessFactor's own established precedent), only
  // PbrClearcoat gives them real rendering meaning. clearcoatFactor
  // default 0.0f (not 1.0f) -- "no clearcoat" is the correct inert
  // default for every other kind, unlike metallicFactor/roughnessFactor
  // whose own 1.0f default predates this convention.
  float clearcoatFactor = 0.0f;
  float clearcoatRoughness = 0.0f;
};

}  // namespace atlantis::asset_system

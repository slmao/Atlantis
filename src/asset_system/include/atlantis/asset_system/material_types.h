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
// round, no real consumer names one yet.
enum class MaterialKind {
  UnlitTextured,
  LitTextured,
};

// CPU-side result of loadMaterialAsset() -- names no RHI type, matching
// TextureAssetData's own discipline exactly. A composition root outside
// Asset System is responsible for resolving textureAsset to real pixel
// data (via loadTextureAsset()) and constructing any RHI Sampler/
// SampledTexture/Material from these fields.
struct MaterialAssetData {
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
};

}  // namespace atlantis::asset_system

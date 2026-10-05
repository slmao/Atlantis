#pragma once

#include <atlantis/schema.h>

#include <span>

namespace atlantis::asset_system {

// Plan 0048 P1/P8 (Spec 0048 R5/R6, ADR-0099 D2): descriptors of
// MaterialAssetData, MaterialKind, MaterialAlphaMode, MaterialSamplerFilter,
// MaterialSamplerAddressMode and EntityRef, in that order (a stable listing
// order, not identity). Immutable static data; safe for concurrent reads.
[[nodiscard]] std::span<const schema::TypeDescriptor> assetSystemSchema() noexcept;

}  // namespace atlantis::asset_system

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <atlantis/result.h>
#include <atlantis/rhi/types.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system::rhi_integration {

// Caller-supplied, Mesh/vertex-schema-owned data -- exactly the two
// fields ReflectionMetadata's own VertexInputAttribute deliberately
// omits (ADR-0030's "vertex stride is host-C++-owned, never
// shader-owned" decision). One entry per attribute, matched to
// ReflectionMetadata::vertexInputAttributes by `location` (not by array
// position -- order is not assumed to match).
struct MeshVertexAttributeSchema {
  std::uint32_t location = 0;
  std::uint32_t offsetBytes = 0;
};

enum class MappingError {
  AttributeCountMismatch,      // reflected attribute count != schema entry count
  LocationNotFoundInSchema,    // a reflected location has no matching schema entry
  UnsupportedVertexAttributeType,  // mirrors TransformError's own case, re-checked here
                                    // defensively since this metadata may have been loaded
                                    // from disk independently of the compile that produced it
};

// Combines vertexMetadata's reflected {location, type} pairs with
// schema's caller-supplied {location, offsetBytes} pairs into RHI's
// existing VertexInputLayout -- cross-validates (does not invent):
// every reflected location must have a matching schema entry, and vice
// versa. strideBytes is a direct, un-cross-validated pass-through of
// the strideBytes parameter below -- there is no reflected value to
// cross-validate it against.
[[nodiscard]] atlantis::Result<atlantis::rhi::VertexInputLayout, MappingError> toVertexInputLayout(
    const ReflectionMetadata& vertexMetadata, const std::vector<MeshVertexAttributeSchema>& schema,
    std::uint32_t strideBytes);

// Sums metadata.pushConstantRanges (this round: expected to be exactly
// one range) into the single std::size_t
// PipelineCreateParams::pushConstantSizeBytes expects. Returns 0 (not
// an error) if metadata has no push-constant ranges at all -- a
// legitimate, if unused-by-this-round's-material, case.
[[nodiscard]] std::size_t toPushConstantSize(const ReflectionMetadata& metadata);

}  // namespace atlantis::shader_system::rhi_integration

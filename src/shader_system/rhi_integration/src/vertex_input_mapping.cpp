#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>

#include <algorithm>
#include <optional>

namespace atlantis::shader_system::rhi_integration {

namespace {

[[nodiscard]] std::optional<atlantis::rhi::VertexAttributeFormat> toRhiFormat(VertexAttributeType type) {
  switch (type) {
    case VertexAttributeType::Float3:
      return atlantis::rhi::VertexAttributeFormat::Float3;
    case VertexAttributeType::Float2:
      return atlantis::rhi::VertexAttributeFormat::Float2;
  }
  return std::nullopt;
}

[[nodiscard]] const MeshVertexAttributeSchema* findByLocation(const std::vector<MeshVertexAttributeSchema>& schema,
                                                                std::uint32_t location) {
  const auto it = std::find_if(schema.begin(), schema.end(),
                                [location](const MeshVertexAttributeSchema& entry) { return entry.location == location; });
  return it == schema.end() ? nullptr : &(*it);
}

}  // namespace

atlantis::Result<atlantis::rhi::VertexInputLayout, MappingError> toVertexInputLayout(
    const ReflectionMetadata& vertexMetadata, const std::vector<MeshVertexAttributeSchema>& schema,
    std::uint32_t strideBytes) {
  using ResultType = atlantis::Result<atlantis::rhi::VertexInputLayout, MappingError>;

  if (vertexMetadata.vertexInputAttributes.size() != schema.size()) {
    return ResultType::Err(MappingError::AttributeCountMismatch);
  }

  atlantis::rhi::VertexInputLayout layout;
  layout.strideBytes = strideBytes;

  for (const VertexInputAttribute& reflected : vertexMetadata.vertexInputAttributes) {
    const MeshVertexAttributeSchema* schemaEntry = findByLocation(schema, reflected.location);
    if (schemaEntry == nullptr) return ResultType::Err(MappingError::LocationNotFoundInSchema);

    const auto format = toRhiFormat(reflected.type);
    if (!format.has_value()) return ResultType::Err(MappingError::UnsupportedVertexAttributeType);

    layout.attributes.push_back(atlantis::rhi::VertexAttribute{
        .location = reflected.location, .offsetBytes = schemaEntry->offsetBytes, .format = *format});
  }

  // The reverse direction: every schema entry must also have a matching
  // reflected attribute -- with equal sizes and every reflected
  // location already matched uniquely above, this only fails if
  // `schema` itself contains a location not present among the reflected
  // attributes (e.g. a caller-side typo), which the forward pass alone
  // cannot detect.
  for (const MeshVertexAttributeSchema& schemaEntry : schema) {
    const bool found = std::any_of(
        vertexMetadata.vertexInputAttributes.begin(), vertexMetadata.vertexInputAttributes.end(),
        [&](const VertexInputAttribute& reflected) { return reflected.location == schemaEntry.location; });
    if (!found) return ResultType::Err(MappingError::LocationNotFoundInSchema);
  }

  return ResultType::Ok(std::move(layout));
}

std::size_t toPushConstantSize(const ReflectionMetadata& metadata) {
  std::size_t total = 0;
  for (const PushConstantRange& range : metadata.pushConstantRanges) total += range.sizeBytes;
  return total;
}

}  // namespace atlantis::shader_system::rhi_integration

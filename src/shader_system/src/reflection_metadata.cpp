#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

bool operator==(const DescriptorBinding& lhs, const DescriptorBinding& rhs) {
  return lhs.set == rhs.set && lhs.binding == rhs.binding && lhs.type == rhs.type && lhs.stage == rhs.stage;
}

bool operator==(const PushConstantRange& lhs, const PushConstantRange& rhs) {
  return lhs.offsetBytes == rhs.offsetBytes && lhs.sizeBytes == rhs.sizeBytes && lhs.stage == rhs.stage;
}

bool operator==(const VertexInputAttribute& lhs, const VertexInputAttribute& rhs) {
  return lhs.location == rhs.location && lhs.type == rhs.type;
}

bool operator==(const ReflectionMetadata& lhs, const ReflectionMetadata& rhs) {
  return lhs.schemaVersion == rhs.schemaVersion && lhs.entryPointName == rhs.entryPointName &&
         lhs.stage == rhs.stage && lhs.descriptorBindings == rhs.descriptorBindings &&
         lhs.pushConstantRanges == rhs.pushConstantRanges &&
         lhs.vertexInputAttributes == rhs.vertexInputAttributes &&
         lhs.varyingOutputLocations == rhs.varyingOutputLocations &&
         lhs.varyingInputLocations == rhs.varyingInputLocations && lhs.sdkProvenance == rhs.sdkProvenance;
}

}  // namespace atlantis::shader_system

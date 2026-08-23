#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

bool operator==(const Extent2D& lhs, const Extent2D& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

bool operator==(const ClearColorValue& lhs, const ClearColorValue& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool operator==(const BufferCreateParams& lhs, const BufferCreateParams& rhs) {
  return lhs.purpose == rhs.purpose && lhs.sizeBytes == rhs.sizeBytes;
}

bool operator==(const TextureCreateParams& lhs, const TextureCreateParams& rhs) {
  return lhs.extent == rhs.extent && lhs.format == rhs.format;
}

bool operator==(const SampledTextureCreateParams& lhs, const SampledTextureCreateParams& rhs) {
  return lhs.extent == rhs.extent && lhs.format == rhs.format;
}

bool operator==(const SamplerCreateParams& lhs, const SamplerCreateParams& rhs) {
  return lhs.filter == rhs.filter && lhs.addressMode == rhs.addressMode;
}

bool operator==(const OffscreenTargetCreateParams& lhs, const OffscreenTargetCreateParams& rhs) {
  return lhs.extent == rhs.extent && lhs.format == rhs.format;
}

bool operator==(const VertexAttribute& lhs, const VertexAttribute& rhs) {
  return lhs.location == rhs.location && lhs.offsetBytes == rhs.offsetBytes && lhs.format == rhs.format;
}

bool operator==(const VertexInputLayout& lhs, const VertexInputLayout& rhs) {
  return lhs.strideBytes == rhs.strideBytes && lhs.attributes == rhs.attributes;
}

}  // namespace atlantis::rhi

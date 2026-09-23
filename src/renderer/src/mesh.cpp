#include <atlantis/renderer/mesh.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include <atlantis/assert.h>

#include "mesh_bounds.h"

namespace atlantis::renderer {

Mesh::Mesh(std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer, std::unique_ptr<atlantis::rhi::Buffer> indexBuffer,
           std::uint32_t indexCount, std::array<float, 3> localBoundsCentre) noexcept
    : vertexBuffer_(std::move(vertexBuffer)),
      indexBuffer_(std::move(indexBuffer)),
      indexCount_(indexCount),
      localBoundsCentre_(localBoundsCentre) {}

std::array<float, 3> computeLocalBoundsCentre(const atlantis::rhi::VertexInputLayout& layout, const void* vertexData,
                                              std::size_t vertexDataSizeBytes) {
  constexpr std::array<float, 3> kOrigin{0.0f, 0.0f, 0.0f};
  if (layout.strideBytes == 0) {
    ATLANTIS_CHECK_MSG(false, "createMesh(): the vertex layout must have a non-zero stride");
    return kOrigin;
  }
  if (vertexDataSizeBytes % layout.strideBytes != 0) {
    ATLANTIS_CHECK_MSG(false, "createMesh(): the vertex data size must be a multiple of the layout stride");
    return kOrigin;
  }
  const auto position = std::find_if(layout.attributes.begin(), layout.attributes.end(),
                                     [](const atlantis::rhi::VertexAttribute& attribute) { return attribute.location == 0; });
  if (position == layout.attributes.end() || position->format != atlantis::rhi::VertexAttributeFormat::Float3 ||
      position->offsetBytes + 3 * sizeof(float) > layout.strideBytes) {
    ATLANTIS_CHECK_MSG(false, "createMesh(): the vertex layout must place a Float3 position at location 0");
    return kOrigin;
  }

  const std::size_t vertexCount = vertexDataSizeBytes / layout.strideBytes;
  if (vertexCount == 0) return kOrigin;
  std::array<float, 3> minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
  std::array<float, 3> maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest()};
  const auto* bytes = static_cast<const unsigned char*>(vertexData);
  for (std::size_t v = 0; v < vertexCount; ++v) {
    float xyz[3];
    std::memcpy(xyz, bytes + v * layout.strideBytes + position->offsetBytes, sizeof(xyz));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      minimum[axis] = std::min(minimum[axis], xyz[axis]);
      maximum[axis] = std::max(maximum[axis], xyz[axis]);
    }
  }
  return {(minimum[0] + maximum[0]) * 0.5f, (minimum[1] + maximum[1]) * 0.5f, (minimum[2] + maximum[2]) * 0.5f};
}

atlantis::Result<Mesh, CreateMeshError> createMesh(atlantis::rhi::Device& device,
                                                    atlantis::rhi::VertexInputLayout layout, const void* vertexData,
                                                    std::size_t vertexDataSizeBytes, const std::uint16_t* indices,
                                                    std::uint32_t indexCount) {
  using ResultT = atlantis::Result<Mesh, CreateMeshError>;

  // The layout is inspected exactly once, here at creation, to compute
  // the bounds centre (its stride and location-0 position offset only --
  // computeLocalBoundsCentre()); it is never re-inspected or stored. RHI
  // still does not parse or reflect vertex layouts (ADR-0027). Plan 0042
  // Milestone 3 (Q2): a narrow ADR-0027-adjacent invariant correction,
  // human-ruled 2026-09-23.
  const std::array<float, 3> localBoundsCentre = computeLocalBoundsCentre(layout, vertexData, vertexDataSizeBytes);

  auto vertexBufferResult = device.createBuffer(
      {.purpose = atlantis::rhi::BufferPurpose::Vertex, .sizeBytes = vertexDataSizeBytes});
  if (vertexBufferResult.isErr()) {
    return ResultT::Err(CreateMeshError::VertexBufferCreationFailed);
  }
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer = std::move(vertexBufferResult.value());
  std::memcpy(vertexBuffer->mappedData(), vertexData, vertexDataSizeBytes);

  const std::size_t indexDataSizeBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint16_t);
  auto indexBufferResult =
      device.createBuffer({.purpose = atlantis::rhi::BufferPurpose::Index, .sizeBytes = indexDataSizeBytes});
  if (indexBufferResult.isErr()) {
    return ResultT::Err(CreateMeshError::IndexBufferCreationFailed);
  }
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer = std::move(indexBufferResult.value());
  std::memcpy(indexBuffer->mappedData(), indices, indexDataSizeBytes);

  return ResultT::Ok(Mesh(std::move(vertexBuffer), std::move(indexBuffer), indexCount, localBoundsCentre));
}


// Plan 0039 P4: a deliberate near-copy of the overload above rather than
// a shared helper taking an index stride. The u16 body must stay
// provably untouched for Spec 0039's "the 16-bit path is byte-identical"
// claim, and a shared helper would turn that from a diff into a reading
// exercise. The two differences are marked.
atlantis::Result<Mesh, CreateMeshError> createMesh(atlantis::rhi::Device& device,
                                                    atlantis::rhi::VertexInputLayout layout, const void* vertexData,
                                                    std::size_t vertexDataSizeBytes, const std::uint32_t* indices,
                                                    std::uint32_t indexCount) {
  using ResultT = atlantis::Result<Mesh, CreateMeshError>;

  // As in the overload above: the layout's single, creation-time use.
  const std::array<float, 3> localBoundsCentre = computeLocalBoundsCentre(layout, vertexData, vertexDataSizeBytes);

  auto vertexBufferResult = device.createBuffer(
      {.purpose = atlantis::rhi::BufferPurpose::Vertex, .sizeBytes = vertexDataSizeBytes});
  if (vertexBufferResult.isErr()) {
    return ResultT::Err(CreateMeshError::VertexBufferCreationFailed);
  }
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer = std::move(vertexBufferResult.value());
  std::memcpy(vertexBuffer->mappedData(), vertexData, vertexDataSizeBytes);

  // Difference 1: four bytes per index, not two.
  const std::size_t indexDataSizeBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
  // Difference 2: the Buffer records the width its bytes were written
  // at, so VulkanCommandList::bindIndexBuffer() binds VK_INDEX_TYPE_UINT32
  // without this call site or any bind site saying so.
  auto indexBufferResult = device.createBuffer({.purpose = atlantis::rhi::BufferPurpose::Index,
                                                 .sizeBytes = indexDataSizeBytes,
                                                 .indexType = atlantis::rhi::IndexType::Uint32});
  if (indexBufferResult.isErr()) {
    return ResultT::Err(CreateMeshError::IndexBufferCreationFailed);
  }
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer = std::move(indexBufferResult.value());
  std::memcpy(indexBuffer->mappedData(), indices, indexDataSizeBytes);

  return ResultT::Ok(Mesh(std::move(vertexBuffer), std::move(indexBuffer), indexCount, localBoundsCentre));
}

}  // namespace atlantis::renderer

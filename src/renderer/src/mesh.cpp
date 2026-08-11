#include <atlantis/renderer/mesh.h>

#include <cstring>

namespace atlantis::renderer {

Mesh::Mesh(std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer, std::unique_ptr<atlantis::rhi::Buffer> indexBuffer,
           std::uint32_t indexCount) noexcept
    : vertexBuffer_(std::move(vertexBuffer)), indexBuffer_(std::move(indexBuffer)), indexCount_(indexCount) {}

atlantis::Result<Mesh, CreateMeshError> createMesh(atlantis::rhi::Device& device,
                                                    atlantis::rhi::VertexInputLayout layout, const void* vertexData,
                                                    std::size_t vertexDataSizeBytes, const std::uint16_t* indices,
                                                    std::uint32_t indexCount) {
  using ResultT = atlantis::Result<Mesh, CreateMeshError>;

  // layout is not otherwise inspected by createMesh() itself -- RHI does
  // not parse or reflect vertex layouts (ADR-0027); it is threaded through
  // only so a future caller does not need a second, separate way to
  // describe a Mesh's own vertex format. Reserved, not currently read.
  static_cast<void>(layout);

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

  return ResultT::Ok(Mesh(std::move(vertexBuffer), std::move(indexBuffer), indexCount));
}

}  // namespace atlantis::renderer

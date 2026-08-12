#pragma once

#include <cstdint>
#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/types.h>

namespace atlantis::renderer {

// Owns exactly one vertex Buffer and one index Buffer (ADR-0022). Move-
// only, single-owner. Constructed once; never re-uploaded or mutated.
// Not internally thread-safe; caller-thread-only (ADR-0004).
class Mesh {
 public:
  Mesh(std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer, std::unique_ptr<atlantis::rhi::Buffer> indexBuffer,
       std::uint32_t indexCount) noexcept;
  ~Mesh() = default;

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&&) noexcept = default;
  Mesh& operator=(Mesh&&) noexcept = default;

  [[nodiscard]] atlantis::rhi::Buffer& vertexBuffer() const noexcept { return *vertexBuffer_; }
  [[nodiscard]] atlantis::rhi::Buffer& indexBuffer() const noexcept { return *indexBuffer_; }
  [[nodiscard]] std::uint32_t indexCount() const noexcept { return indexCount_; }

 private:
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer_;
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer_;
  std::uint32_t indexCount_;
};

enum class CreateMeshError {
  VertexBufferCreationFailed,
  IndexBufferCreationFailed,
};

// Convenience free function -- NOT a Renderer method (ADR-0022: Renderer
// never creates a Mesh). Creates both Buffers via device, copies
// vertexData/indexData into their mapped memory once, and returns an
// independently-owned Mesh. Each call produces a new, independent Mesh --
// no cache, no deduplication.
[[nodiscard]] atlantis::Result<Mesh, CreateMeshError> createMesh(atlantis::rhi::Device& device,
                                                                  atlantis::rhi::VertexInputLayout layout,
                                                                  const void* vertexData,
                                                                  std::size_t vertexDataSizeBytes,
                                                                  const std::uint16_t* indices,
                                                                  std::uint32_t indexCount);

}  // namespace atlantis::renderer

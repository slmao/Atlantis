#pragma once

#include <array>
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
       std::uint32_t indexCount, std::array<float, 3> localBoundsCentre = {0.0f, 0.0f, 0.0f}) noexcept;
  ~Mesh() = default;

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&&) noexcept = default;
  Mesh& operator=(Mesh&&) noexcept = default;

  [[nodiscard]] atlantis::rhi::Buffer& vertexBuffer() const noexcept { return *vertexBuffer_; }
  [[nodiscard]] atlantis::rhi::Buffer& indexBuffer() const noexcept { return *indexBuffer_; }
  [[nodiscard]] std::uint32_t indexCount() const noexcept { return indexCount_; }
  // Plan 0042 Milestone 3 (ADR-0090 Decision 2): the local-space bounds
  // centre createMesh() computed from the vertex positions -- the point a
  // blended draw is sorted by, after objectToWorld. Immutable.
  [[nodiscard]] const std::array<float, 3>& localBoundsCentre() const noexcept { return localBoundsCentre_; }

 private:
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer_;
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer_;
  std::uint32_t indexCount_;
  std::array<float, 3> localBoundsCentre_;
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

// Spec 0039/ADR-0086: the 32-bit-index sibling, for .amesh schema-5
// meshes (the glTF importer's own). An overload rather than a widened
// signature, so all 17 existing createMesh() calls compile and behave
// exactly as before. Mesh itself is identical either way -- the width
// lives in the index Buffer this creates, which is why Mesh gains no
// new state and ADR-0022's ownership model is untouched.
//
// Index values must be at or below the drawable ceiling the Asset
// System's loader already enforces (kMaxDrawableIndexValue, ruling O1);
// this function does not re-scan them.
[[nodiscard]] atlantis::Result<Mesh, CreateMeshError> createMesh(atlantis::rhi::Device& device,
                                                                  atlantis::rhi::VertexInputLayout layout,
                                                                  const void* vertexData,
                                                                  std::size_t vertexDataSizeBytes,
                                                                  const std::uint32_t* indices,
                                                                  std::uint32_t indexCount);

}  // namespace atlantis::renderer

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Spec 0039/ADR-0087: the index width a StaticMeshAssetData carries.
// Deliberately Asset System's own type, parallel to rhi::IndexType and
// never the same one -- ADR-0043's Decision forbids naming any RHI type
// here, and tests/asset_system/module_boundary_tests.cpp enforces that
// automatically. The composition root translates between the two.
enum class MeshIndexType {
  Uint16,  // .amesh schema 4 -- everything the cooker produces
  Uint32,  // .amesh schema 5 -- the glTF importer's own meshes
};

// Spec 0039 ruling O1 / ADR-0086 Decision item 6: fullDrawIndexUint32 is
// deliberately left disabled, so the only ceiling on a 32-bit index value
// the Vulkan specification guarantees is maxDrawIndexedIndexValue's own
// floor of 2^24-1. Plain numbers, not RHI types, so ADR-0043's Core-only
// boundary is untouched.
inline constexpr std::uint32_t kMaxDrawableIndexValue = (1U << 24) - 1U;  // 16,777,215

// The bound loadStaticMeshAsset() actually applies. decodeMeshArtifactU32()
// already rejects any index >= vertex_count, so bounding the vertex count
// bounds every index value exactly -- an O(1) header check, never a
// per-index pass (Plan 0039 P2). A mesh of exactly this many vertices has
// a largest possible index of kMaxDrawableIndexValue, which is legal.
inline constexpr std::uint32_t kMaxDrawableVertexCount = kMaxDrawableIndexValue + 1U;  // 2^24

// CPU-side static mesh data produced by cook() (Step 4) and load()
// (Step 5). Never touches an RHI/Renderer type (ADR-0043) -- a
// composition root outside Asset System is responsible for passing
// vertexBytes()/indices() into atlantis::renderer::createMesh(). Not
// thread-safe; caller-thread-only (ADR-0004). Single owner, RAII;
// move-only by virtue of its std::vector members. No manual cleanup
// step.
//
// Spec 0039/ADR-0087: carries either 16-bit or 32-bit indices, never
// both and never a converted copy of the other. indexType() says which,
// and the two typed accessors are each valid only for their own width.
class StaticMeshAssetData {
 public:
  StaticMeshAssetData(std::vector<std::byte> vertexBytes, std::vector<std::uint16_t> indices,
                       std::uint32_t vertexStrideBytes) noexcept;

  // Spec 0039: the schema-5 sibling. Same shape, 32-bit indices.
  StaticMeshAssetData(std::vector<std::byte> vertexBytes, std::vector<std::uint32_t> indices,
                       std::uint32_t vertexStrideBytes) noexcept;

  [[nodiscard]] const std::vector<std::byte>& vertexBytes() const noexcept { return vertexBytes_; }
  [[nodiscard]] std::uint32_t vertexStrideBytes() const noexcept { return vertexStrideBytes_; }

  [[nodiscard]] MeshIndexType indexType() const noexcept { return indexType_; }

  // Precondition: indexType() == the accessor's own width. Reading the
  // wrong one is a programmer error (ATLANTIS_CHECK, evaluated in Release
  // too), never an empty vector -- a caller written only for schema 4
  // that meets a schema-5 asset must stop, not silently draw nothing
  // (Spec 0039 ruling O2, ADR-0087 Decision item 2).
  [[nodiscard]] const std::vector<std::uint16_t>& indices() const noexcept;
  [[nodiscard]] const std::vector<std::uint32_t>& indices32() const noexcept;

  // vertexBytes().size() / vertexStrideBytes() -- a precondition
  // violation (zero stride) is a programmer error (ATLANTIS_CHECK), not
  // a recoverable error, since every producer of this type validates
  // stride before construction.
  [[nodiscard]] std::uint32_t vertexCount() const noexcept;

  // Width-independent by design, and therefore NOT guarded: it reports
  // whichever index vector this object actually carries, so an existing
  // caller that only wants a count keeps working for both schemas.
  [[nodiscard]] std::uint32_t indexCount() const noexcept;

 private:
  std::vector<std::byte> vertexBytes_;
  std::vector<std::uint16_t> indices_;
  std::vector<std::uint32_t> indices32_;
  std::uint32_t vertexStrideBytes_;
  MeshIndexType indexType_;
};

}  // namespace atlantis::asset_system

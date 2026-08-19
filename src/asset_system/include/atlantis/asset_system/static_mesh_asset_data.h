#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// CPU-side static mesh data produced by cook() (Step 4) and load()
// (Step 5). Never touches an RHI/Renderer type (ADR-0043) -- a
// composition root outside Asset System is responsible for passing
// vertexBytes()/indices() into atlantis::renderer::createMesh(). Not
// thread-safe; caller-thread-only (ADR-0004). Single owner, RAII;
// move-only by virtue of its std::vector members. No manual cleanup
// step.
class StaticMeshAssetData {
 public:
  StaticMeshAssetData(std::vector<std::byte> vertexBytes, std::vector<std::uint16_t> indices,
                       std::uint32_t vertexStrideBytes) noexcept;

  [[nodiscard]] const std::vector<std::byte>& vertexBytes() const noexcept { return vertexBytes_; }
  [[nodiscard]] const std::vector<std::uint16_t>& indices() const noexcept { return indices_; }
  [[nodiscard]] std::uint32_t vertexStrideBytes() const noexcept { return vertexStrideBytes_; }

  // vertexBytes().size() / vertexStrideBytes() -- a precondition
  // violation (zero stride) is a programmer error (ATLANTIS_CHECK), not
  // a recoverable error, since every producer of this type validates
  // stride before construction.
  [[nodiscard]] std::uint32_t vertexCount() const noexcept;
  [[nodiscard]] std::uint32_t indexCount() const noexcept;

 private:
  std::vector<std::byte> vertexBytes_;
  std::vector<std::uint16_t> indices_;
  std::uint32_t vertexStrideBytes_;
};

}  // namespace atlantis::asset_system

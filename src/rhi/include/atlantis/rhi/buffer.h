#pragma once

#include <cstddef>

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A GPU-visible linear memory region, fixed to one of three purposes at
// creation (ADR-0023). This round, every Buffer is host-visible and
// host-coherent regardless of purpose -- see mappedData()'s own contract
// and ADR-0023's Decision for why (no staging/upload path this round).
// Move-only, single-owner, held behind std::unique_ptr<Buffer> (ADR-0014's
// mechanism, ADR-0023). Not internally thread-safe; caller-thread-only
// (ADR-0004). No hidden cache: Device does not deduplicate or retain a
// reference to any Buffer it creates (ADR-0003).
class Buffer {
 public:
  virtual ~Buffer() = default;

  [[nodiscard]] virtual BufferPurpose purpose() const = 0;
  [[nodiscard]] virtual std::size_t sizeBytes() const = 0;

  // A pointer to this Buffer's host-visible, host-coherent memory, valid
  // for this Buffer's whole lifetime (mapped once, at construction --
  // never remapped). The caller may write directly at any time; no
  // explicit flush/invalidate call is required (host-coherent). Writing
  // to a Uniform-purpose Buffer while GPU work from a prior frame might
  // still read it is a caller precondition violation -- see Spec 0007's
  // write-timing contract, which this round's single-frame-in-flight
  // discipline satisfies structurally for the one caller pattern this
  // spec uses (write once per frame, immediately after
  // acquireNextTarget() returns).
  [[nodiscard]] virtual void* mappedData() = 0;
};

}  // namespace atlantis::rhi

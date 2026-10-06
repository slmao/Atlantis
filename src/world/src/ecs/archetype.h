#pragma once

#include <atlantis/schema.h>
#include <atlantis/world/ecs/component.h>
#include <atlantis/world/ecs/entity_id.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace atlantis::world::ecs {

// Plan 0050 P5 (Spec 0050 R4, R5; ruling J5): an archetype is the sorted set
// of its component TypeIds; it stores rows in fixed-budget chunks. Private to
// the World module's implementation (J2).
inline constexpr std::size_t kChunkBudgetBytes = 16 * 1024;

struct AlignedDelete {
  std::size_t alignment = alignof(std::max_align_t);
  void operator()(std::byte* p) const noexcept { ::operator delete(p, std::align_val_t{alignment}); }
};

// One allocation; columns at fixed offsets, laid out for `capacity` rows.
struct Chunk {
  std::unique_ptr<std::byte[], AlignedDelete> memory;
  std::uint32_t rows = 0;
};

struct Column {
  ComponentInfo info;
  std::size_t offset = 0;  // within a chunk
};

struct Archetype {
  std::vector<schema::TypeId> types;  // sorted by value
  std::vector<Column> columns;        // one per type, same order
  std::uint32_t capacity = 0;         // rows per chunk
  std::size_t chunkBytes = 0;
  std::size_t alignment = 0;
  std::vector<Chunk> chunks;          // every chunk but the last is full

  // Columns laid out for the largest n >= 1 that fits the budget: the
  // EntityId column first, then each component column at its alignment. A row
  // larger than the budget gets n = 1 in a chunk sized to fit.
  Archetype(std::vector<schema::TypeId> sortedTypes, const std::vector<ComponentInfo>& sortedInfos)
      : types(std::move(sortedTypes)) {
    alignment = alignof(EntityId);
    for (const ComponentInfo& info : sortedInfos) alignment = std::max<std::size_t>(alignment, info.alignment);
    const auto layout = [&](std::uint32_t n, std::vector<Column>* out) {
      std::size_t end = sizeof(EntityId) * n;
      for (const ComponentInfo& info : sortedInfos) {
        const std::size_t offset = (end + info.alignment - 1) / info.alignment * info.alignment;
        if (out != nullptr) out->push_back({info, offset});
        end = offset + static_cast<std::size_t>(info.size) * n;
      }
      return end;
    };
    std::size_t rowBytes = sizeof(EntityId);
    for (const ComponentInfo& info : sortedInfos) rowBytes += info.size;
    std::uint32_t n = static_cast<std::uint32_t>(kChunkBudgetBytes / rowBytes);
    while (n > 0 && layout(n, nullptr) > kChunkBudgetBytes) --n;
    if (n == 0) n = 1;
    capacity = n;
    chunkBytes = std::max(layout(n, &columns), kChunkBudgetBytes);
  }

  [[nodiscard]] std::optional<std::size_t> columnOf(schema::TypeId type) const noexcept {
    for (std::size_t i = 0; i < types.size(); ++i) {
      if (types[i] == type) return i;
    }
    return std::nullopt;
  }

  [[nodiscard]] EntityId* entities(std::uint32_t chunk) const noexcept {
    return reinterpret_cast<EntityId*>(chunks[chunk].memory.get());
  }

  [[nodiscard]] std::byte* component(std::uint32_t chunk, std::size_t column, std::uint32_t row) const noexcept {
    return chunks[chunk].memory.get() + columns[column].offset +
           static_cast<std::size_t>(columns[column].info.size) * row;
  }

  [[nodiscard]] std::uint32_t rowCount() const noexcept {
    return chunks.empty() ? 0 : static_cast<std::uint32_t>((chunks.size() - 1) * capacity + chunks.back().rows);
  }
};

}  // namespace atlantis::world::ecs

#pragma once

#include <array>
#include <cstring>
#include <type_traits>

namespace atlantis::world {

// Spec 0051 R4 / ADR-0102 D4 (Plan 0051 P1): an entity's resolved world
// matrix as a component of the baked Runtime World, which carries no
// hierarchy. Column-major, the layout World::getWorldMatrix() returns:
// column3 holds the translation. Default is identity. A plain value; safe
// for concurrent reads.
struct WorldMatrix {
  std::array<float, 4> column0{1.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 4> column1{0.0f, 1.0f, 0.0f, 0.0f};
  std::array<float, 4> column2{0.0f, 0.0f, 1.0f, 0.0f};
  std::array<float, 4> column3{0.0f, 0.0f, 0.0f, 1.0f};
};

static_assert(std::is_standard_layout_v<WorldMatrix>);
static_assert(std::is_trivially_copyable_v<WorldMatrix>);
static_assert(sizeof(WorldMatrix) == 16 * sizeof(float), "no padding: the four columns are contiguous");

// Bit-exact conversions to and from World::getWorldMatrix()'s array: a byte
// copy, so the baked matrix is the authoring stage's matrix bit for bit.
[[nodiscard]] inline WorldMatrix toWorldMatrix(const std::array<float, 16>& columnMajor) noexcept {
  WorldMatrix m;
  std::memcpy(&m, columnMajor.data(), sizeof(WorldMatrix));
  return m;
}

[[nodiscard]] inline std::array<float, 16> toColumnMajor(const WorldMatrix& m) noexcept {
  std::array<float, 16> columnMajor{};
  std::memcpy(columnMajor.data(), &m, sizeof(WorldMatrix));
  return columnMajor;
}

}  // namespace atlantis::world

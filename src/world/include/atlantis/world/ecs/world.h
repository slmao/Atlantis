#pragma once

#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/ecs/component.h>
#include <atlantis/world/ecs/ecs_error.h>
#include <atlantis/world/ecs/entity_id.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace atlantis::world::ecs {

struct Archetype;  // complete definition private to src/world/src/ecs/archetype.h
class CommandBuffer;

namespace detail {

// One chunk's rows, handed by the type-erased walk to query<Ts...>(). columns[i]
// is the column of the query's i-th type, in the query's own order.
struct ChunkView {
  std::size_t rows = 0;
  const EntityId* entities = nullptr;
  std::byte* const* columns = nullptr;
};

using ChunkVisitor = void (*)(void* context, const ChunkView& view);

struct EntityRecord {
  std::uint64_t generation = 0;
  bool alive = false;
  std::uint32_t archetype = 0;
  std::uint32_t chunk = 0;
  std::uint32_t row = 0;
};

struct GuidBinding;  // the creation-time binding in entity_guid_map.h

}  // namespace detail

// Spec 0050 / ADR-0101: Atlantis's archetype-and-chunk entity-component store
// -- additive beside world::World (ruling Q1), which it neither replaces nor
// wraps. Entities with the same set of component types share an archetype;
// each archetype stores its rows in 16 KiB chunks, one column per component
// (Plan 0050 P5). Component identity is schema::TypeId (component.h).
//
// Point access (get/set) is by value. query<Ts...>(fn) hands fn references
// into chunk columns, valid only for that one call (ruling Q6); create,
// destroy, add and remove are structural and are refused -- an assertion in
// Debug, StructuralChangeDuringQuery in every build -- while a query runs
// (Plan 0050 J1); defer them through a CommandBuffer. get/set/has stay allowed
// inside a query (J8). Iteration order is archetype creation order, then
// chunk, then row.
//
// Not thread-safe (ADR-0004): one thread uses a World at a time. Instances
// are independent; there is no global state.
class World {
 public:
  World();
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) = delete;

  // A new entity with no components. Returns kInvalidEntityId, changing
  // nothing, when called during a query (Plan 0050 J1).
  [[nodiscard]] EntityId createEntity();
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> destroyEntity(EntityId id);
  [[nodiscard]] bool isValid(EntityId id) const noexcept;

  // ComponentAlreadyPresent if the entity has a T; never overwrites (J6).
  template <Component T>
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> add(EntityId id) {
    return add<T>(id, T{});
  }
  template <Component T>
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> add(EntityId id, const T& value) {
    return addComponent(id, componentInfo<T>(), &value);
  }

  template <Component T>
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> remove(EntityId id) {
    return removeComponent(id, componentTypeId<T>());
  }

  template <Component T>
  [[nodiscard]] atlantis::Result<bool, EcsError> has(EntityId id) const {
    return hasComponent(id, componentTypeId<T>());
  }

  template <Component T>
  [[nodiscard]] atlantis::Result<T, EcsError> get(EntityId id) const {
    T value{};
    const auto read = readComponent(id, componentInfo<T>(), &value);
    if (read.isErr()) return atlantis::Result<T, EcsError>::Err(read.error());
    return atlantis::Result<T, EcsError>::Ok(value);
  }

  // ComponentMissing if the entity has no T; never adds one (J6).
  template <Component T>
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> set(EntityId id, const T& value) {
    return writeComponent(id, componentInfo<T>(), &value);
  }

  // Calls fn(EntityId, Ts&...) for every entity whose archetype contains every
  // Ts. A const-qualified Ts passes const T&.
  template <typename... Ts, typename Fn>
    requires(Component<std::remove_const_t<Ts>> && ...)
  void query(Fn&& fn) {
    const std::array<schema::TypeId, sizeof...(Ts)> ids{componentTypeId<std::remove_const_t<Ts>>()...};
    auto visit = [&fn](const detail::ChunkView& view) {
      for (std::size_t row = 0; row < view.rows; ++row) {
        invokeRow<Ts...>(fn, view, row, std::index_sequence_for<Ts...>{});
      }
    };
    using Visit = decltype(visit);
    forEachMatchingChunk(
        ids, [](void* context, const detail::ChunkView& view) { (*static_cast<Visit*>(context))(view); }, &visit);
  }

 private:
  friend class CommandBuffer;
  friend struct detail::GuidBinding;
  friend struct EcsTestAccess;  // tests only (Plan 0050 J2)

  template <typename... Ts, typename Fn, std::size_t... I>
  static void invokeRow(Fn& fn, const detail::ChunkView& view, std::size_t row, std::index_sequence<I...>) {
    fn(view.entities[row], reinterpret_cast<Ts*>(view.columns[I])[row]...);
  }

  [[nodiscard]] atlantis::Result<std::monostate, EcsError> addComponent(EntityId id, const ComponentInfo& info,
                                                                       const void* bytes);
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> removeComponent(EntityId id, schema::TypeId type);
  [[nodiscard]] atlantis::Result<bool, EcsError> hasComponent(EntityId id, schema::TypeId type) const;
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> readComponent(EntityId id, const ComponentInfo& info,
                                                                        void* out) const;
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> writeComponent(EntityId id, const ComponentInfo& info,
                                                                         const void* bytes);
  void forEachMatchingChunk(std::span<const schema::TypeId> types, detail::ChunkVisitor visitor, void* context);

  [[nodiscard]] bool iterating() const noexcept { return queryDepth_ > 0; }
  [[nodiscard]] bool refuseIfIterating() const;  // asserts in Debug; true when refused
  [[nodiscard]] atlantis::Result<std::monostate, EcsError> validate(EntityId id) const;
  [[nodiscard]] std::uint32_t archetypeFor(std::vector<schema::TypeId> sortedTypes, std::span<const ComponentInfo> infos);
  void moveRow(EntityId id, std::uint32_t targetArchetype, const ComponentInfo* added, const void* addedBytes);
  void removeRow(std::uint32_t archetype, std::uint32_t chunk, std::uint32_t row);

  // Tests only (EcsTestAccess).
  [[nodiscard]] EntityId forceGenerationForTesting(EntityId id, std::uint64_t generation);
  [[nodiscard]] std::size_t archetypeCountForTesting() const noexcept { return archetypes_.size(); }
  [[nodiscard]] std::size_t chunkCountForTesting(std::span<const schema::TypeId> sortedTypes) const;
  [[nodiscard]] std::uint32_t capacityForTesting(std::span<const schema::TypeId> sortedTypes) const;

  std::unique_ptr<EcsWorldIdentity> identity_;
  std::vector<detail::EntityRecord> entities_;
  std::vector<std::uint32_t> freeList_;  // LIFO
  std::vector<std::unique_ptr<Archetype>> archetypes_;  // creation order; [0] is the empty archetype
  std::map<std::vector<schema::TypeId>, std::uint32_t> archetypeIndex_;
  std::uint32_t queryDepth_ = 0;
};

}  // namespace atlantis::world::ecs

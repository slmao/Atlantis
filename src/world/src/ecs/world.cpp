#include <atlantis/world/ecs/world.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include <atlantis/assert.h>

#include "archetype.h"

namespace atlantis::world::ecs {

// The heap-allocated per-instance token every EntityId carries (ADR-0049's
// Accepted Amendment, adopted by ADR-0101 D4); its address is the identity.
class EcsWorldIdentity {};

namespace {

using ResultT = atlantis::Result<std::monostate, EcsError>;

// ADR-0049: reaching this generation retires the index permanently.
constexpr std::uint64_t kTombstone = std::numeric_limits<std::uint64_t>::max();

struct QueryDepthGuard {
  explicit QueryDepthGuard(std::uint32_t& depth) : depth_(depth) { ++depth_; }
  ~QueryDepthGuard() { --depth_; }
  QueryDepthGuard(const QueryDepthGuard&) = delete;
  QueryDepthGuard& operator=(const QueryDepthGuard&) = delete;

 private:
  std::uint32_t& depth_;
};

[[nodiscard]] std::byte* entityBytes(const Archetype& archetype, std::uint32_t chunk, std::uint32_t row) noexcept {
  return archetype.chunks[chunk].memory.get() + sizeof(EntityId) * row;
}

// Appends a row holding `id`; component columns are left for the caller.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> appendRow(Archetype& archetype, const EntityId& id) {
  if (archetype.chunks.empty() || archetype.chunks.back().rows == archetype.capacity) {
    Chunk chunk;
    chunk.memory = std::unique_ptr<std::byte[], AlignedDelete>(
        static_cast<std::byte*>(::operator new(archetype.chunkBytes, std::align_val_t{archetype.alignment})),
        AlignedDelete{archetype.alignment});
    archetype.chunks.push_back(std::move(chunk));
  }
  const auto chunk = static_cast<std::uint32_t>(archetype.chunks.size() - 1);
  const std::uint32_t row = archetype.chunks.back().rows++;
  std::memcpy(entityBytes(archetype, chunk, row), &id, sizeof(EntityId));
  return {chunk, row};
}

[[nodiscard]] std::vector<ComponentInfo> infosOf(const Archetype& archetype) {
  std::vector<ComponentInfo> infos;
  infos.reserve(archetype.columns.size());
  for (const Column& column : archetype.columns) infos.push_back(column.info);
  return infos;
}

}  // namespace

World::World() : identity_(std::make_unique<EcsWorldIdentity>()) {
  [[maybe_unused]] const std::uint32_t empty = archetypeFor({}, {});
  ATLANTIS_ASSERT(empty == 0);
}

World::~World() = default;
World::World(World&&) noexcept = default;

bool World::refuseIfIterating() const {
  if (queryDepth_ == 0) return false;
  ATLANTIS_ASSERT_MSG(queryDepth_ == 0,
                      "ecs::World: structural change during a query -- defer it through a CommandBuffer");
  return true;
}

ResultT World::validate(EntityId id) const {
  if (identity_ == nullptr || id.identity_ != identity_.get() || id.index_ >= entities_.size()) {
    return ResultT::Err(EcsError::InvalidEntity);
  }
  const detail::EntityRecord& record = entities_[id.index_];
  if (!record.alive || record.generation != id.generation_) return ResultT::Err(EcsError::InvalidEntity);
  return ResultT::Ok(std::monostate{});
}

bool World::isValid(EntityId id) const noexcept { return validate(id).isOk(); }

std::uint32_t World::archetypeFor(std::vector<schema::TypeId> sortedTypes, std::span<const ComponentInfo> infos) {
  if (const auto found = archetypeIndex_.find(sortedTypes); found != archetypeIndex_.end()) return found->second;
  const auto index = static_cast<std::uint32_t>(archetypes_.size());
  archetypes_.push_back(
      std::make_unique<Archetype>(sortedTypes, std::vector<ComponentInfo>(infos.begin(), infos.end())));
  archetypeIndex_.emplace(std::move(sortedTypes), index);
  return index;
}

EntityId World::createEntity() {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "ecs::World::createEntity() called on a moved-from World");
  if (refuseIfIterating()) return kInvalidEntityId;
  std::uint32_t index = 0;
  if (!freeList_.empty()) {
    index = freeList_.back();
    freeList_.pop_back();
  } else {
    index = static_cast<std::uint32_t>(entities_.size());
    entities_.emplace_back();
  }
  detail::EntityRecord& record = entities_[index];
  record.alive = true;
  const EntityId id{index, record.generation, identity_.get()};
  const auto [chunk, row] = appendRow(*archetypes_[0], id);
  record.archetype = 0;
  record.chunk = chunk;
  record.row = row;
  return id;
}

void World::removeRow(std::uint32_t archetypeIndex, std::uint32_t chunk, std::uint32_t row) {
  Archetype& archetype = *archetypes_[archetypeIndex];
  const auto lastChunk = static_cast<std::uint32_t>(archetype.chunks.size() - 1);
  const std::uint32_t lastRow = archetype.chunks.back().rows - 1;
  if (chunk != lastChunk || row != lastRow) {
    // Swap-remove: the archetype's last row fills the hole.
    EntityId moved;
    std::memcpy(&moved, entityBytes(archetype, lastChunk, lastRow), sizeof(EntityId));
    std::memcpy(entityBytes(archetype, chunk, row), &moved, sizeof(EntityId));
    for (std::size_t column = 0; column < archetype.columns.size(); ++column) {
      std::memcpy(archetype.component(chunk, column, row), archetype.component(lastChunk, column, lastRow),
                  archetype.columns[column].info.size);
    }
    detail::EntityRecord& record = entities_[moved.index_];
    record.chunk = chunk;
    record.row = row;
  }
  if (--archetype.chunks.back().rows == 0) archetype.chunks.pop_back();
}

void World::moveRow(EntityId id, std::uint32_t targetIndex, const ComponentInfo* added, const void* addedBytes) {
  detail::EntityRecord& record = entities_[id.index_];
  const std::uint32_t sourceIndex = record.archetype;
  const std::uint32_t sourceChunk = record.chunk;
  const std::uint32_t sourceRow = record.row;
  const Archetype& source = *archetypes_[sourceIndex];
  Archetype& target = *archetypes_[targetIndex];

  const auto [chunk, row] = appendRow(target, id);
  for (std::size_t column = 0; column < target.columns.size(); ++column) {
    const ComponentInfo& info = target.columns[column].info;
    if (const auto from = source.columnOf(info.id); from.has_value()) {
      std::memcpy(target.component(chunk, column, row), source.component(sourceChunk, *from, sourceRow), info.size);
    } else {
      ATLANTIS_CHECK(added != nullptr && added->id == info.id);
      std::memcpy(target.component(chunk, column, row), addedBytes, info.size);
    }
  }
  removeRow(sourceIndex, sourceChunk, sourceRow);
  record.archetype = targetIndex;
  record.chunk = chunk;
  record.row = row;
}

ResultT World::destroyEntity(EntityId id) {
  if (refuseIfIterating()) return ResultT::Err(EcsError::StructuralChangeDuringQuery);
  if (const auto valid = validate(id); valid.isErr()) return valid;
  detail::EntityRecord& record = entities_[id.index_];
  removeRow(record.archetype, record.chunk, record.row);
  record.alive = false;
  if (++record.generation != kTombstone) freeList_.push_back(id.index_);  // else retired for good
  return ResultT::Ok(std::monostate{});
}

ResultT World::addComponent(EntityId id, const ComponentInfo& info, const void* bytes) {
  if (refuseIfIterating()) return ResultT::Err(EcsError::StructuralChangeDuringQuery);
  if (const auto valid = validate(id); valid.isErr()) return valid;
  const Archetype& source = *archetypes_[entities_[id.index_].archetype];
  if (source.columnOf(info.id).has_value()) return ResultT::Err(EcsError::ComponentAlreadyPresent);

  std::vector<ComponentInfo> infos = infosOf(source);
  infos.push_back(info);
  std::sort(infos.begin(), infos.end(), [](const ComponentInfo& a, const ComponentInfo& b) { return a.id < b.id; });
  std::vector<schema::TypeId> types;
  for (const ComponentInfo& i : infos) types.push_back(i.id);
  moveRow(id, archetypeFor(std::move(types), infos), &info, bytes);
  return ResultT::Ok(std::monostate{});
}

ResultT World::removeComponent(EntityId id, schema::TypeId type) {
  if (refuseIfIterating()) return ResultT::Err(EcsError::StructuralChangeDuringQuery);
  if (const auto valid = validate(id); valid.isErr()) return valid;
  const Archetype& source = *archetypes_[entities_[id.index_].archetype];
  if (!source.columnOf(type).has_value()) return ResultT::Err(EcsError::ComponentMissing);

  std::vector<ComponentInfo> infos = infosOf(source);
  std::erase_if(infos, [type](const ComponentInfo& info) { return info.id == type; });
  std::vector<schema::TypeId> types;
  for (const ComponentInfo& i : infos) types.push_back(i.id);
  moveRow(id, archetypeFor(std::move(types), infos), nullptr, nullptr);
  return ResultT::Ok(std::monostate{});
}

atlantis::Result<bool, EcsError> World::hasComponent(EntityId id, schema::TypeId type) const {
  using HasResult = atlantis::Result<bool, EcsError>;
  if (const auto valid = validate(id); valid.isErr()) return HasResult::Err(valid.error());
  return HasResult::Ok(archetypes_[entities_[id.index_].archetype]->columnOf(type).has_value());
}

ResultT World::readComponent(EntityId id, const ComponentInfo& info, void* out) const {
  if (const auto valid = validate(id); valid.isErr()) return valid;
  const detail::EntityRecord& record = entities_[id.index_];
  const Archetype& archetype = *archetypes_[record.archetype];
  const auto column = archetype.columnOf(info.id);
  if (!column.has_value()) return ResultT::Err(EcsError::ComponentMissing);
  ATLANTIS_ASSERT(archetype.columns[*column].info == info);
  std::memcpy(out, archetype.component(record.chunk, *column, record.row), info.size);
  return ResultT::Ok(std::monostate{});
}

ResultT World::writeComponent(EntityId id, const ComponentInfo& info, const void* bytes) {
  if (const auto valid = validate(id); valid.isErr()) return valid;
  const detail::EntityRecord& record = entities_[id.index_];
  const Archetype& archetype = *archetypes_[record.archetype];
  const auto column = archetype.columnOf(info.id);
  if (!column.has_value()) return ResultT::Err(EcsError::ComponentMissing);
  ATLANTIS_ASSERT(archetype.columns[*column].info == info);
  std::memcpy(archetype.component(record.chunk, *column, record.row), bytes, info.size);
  return ResultT::Ok(std::monostate{});
}

void World::forEachMatchingChunk(std::span<const schema::TypeId> types, detail::ChunkVisitor visitor,
                                 void* context) {
  const QueryDepthGuard guard{queryDepth_};
  std::vector<std::size_t> columnIndex(types.size());
  std::vector<std::byte*> columns(types.size());
  for (const auto& archetypePtr : archetypes_) {
    const Archetype& archetype = *archetypePtr;
    bool matches = true;
    for (std::size_t i = 0; i < types.size() && matches; ++i) {
      const auto column = archetype.columnOf(types[i]);
      matches = column.has_value();
      if (matches) columnIndex[i] = *column;
    }
    if (!matches) continue;
    for (std::uint32_t chunk = 0; chunk < archetype.chunks.size(); ++chunk) {
      for (std::size_t i = 0; i < types.size(); ++i) {
        columns[i] = archetype.chunks[chunk].memory.get() + archetype.columns[columnIndex[i]].offset;
      }
      const detail::ChunkView view{archetype.chunks[chunk].rows, archetype.entities(chunk), columns.data()};
      visitor(context, view);
    }
  }
}

EntityId World::forceGenerationForTesting(EntityId id, std::uint64_t generation) {
  ATLANTIS_CHECK_MSG(validate(id).isOk(), "forceGenerationForTesting(): id does not name a live entity");
  detail::EntityRecord& record = entities_[id.index_];
  record.generation = generation;
  const EntityId updated{id.index_, generation, identity_.get()};
  std::memcpy(entityBytes(*archetypes_[record.archetype], record.chunk, record.row), &updated, sizeof(EntityId));
  return updated;
}

std::size_t World::chunkCountForTesting(std::span<const schema::TypeId> sortedTypes) const {
  const auto found = archetypeIndex_.find(std::vector<schema::TypeId>(sortedTypes.begin(), sortedTypes.end()));
  return found == archetypeIndex_.end() ? 0 : archetypes_[found->second]->chunks.size();
}

std::uint32_t World::capacityForTesting(std::span<const schema::TypeId> sortedTypes) const {
  const auto found = archetypeIndex_.find(std::vector<schema::TypeId>(sortedTypes.begin(), sortedTypes.end()));
  return found == archetypeIndex_.end() ? 0 : archetypes_[found->second]->capacity;
}

}  // namespace atlantis::world::ecs

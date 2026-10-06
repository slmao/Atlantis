#pragma once

#include <atlantis/schema.h>
#include <atlantis/world/ecs/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::world::ecs {

// Plan 0050 J2: the tests' one view into ecs::World's internal storage, the
// EntityLifecycleTestAccess precedent. One definition, shared by every ECS
// test that needs it, so the friend class has a single definition.
struct EcsTestAccess {
  static EntityId forceGeneration(World& world, EntityId id, std::uint64_t generation) {
    return world.forceGenerationForTesting(id, generation);
  }
  static std::size_t archetypeCount(const World& world) { return world.archetypeCountForTesting(); }
  static std::size_t chunkCount(const World& world, std::vector<schema::TypeId> types) {
    std::sort(types.begin(), types.end());
    return world.chunkCountForTesting(types);
  }
  static std::uint32_t capacity(const World& world, std::vector<schema::TypeId> types) {
    std::sort(types.begin(), types.end());
    return world.capacityForTesting(types);
  }
};

}  // namespace atlantis::world::ecs

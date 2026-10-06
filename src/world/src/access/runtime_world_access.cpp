#include <atlantis/world/access/runtime_world_access.h>

#include "property_access.h"

#include <atlantis/assert.h>
#include <atlantis/world/ecs/entity_guid_map.h>
#include <atlantis/world/ecs/world.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::world::access {

using atlantis::asset_system::EntityGuid;

std::string_view toString(AccessError error) noexcept {
  switch (error) {
    case AccessError::UnknownEntity: return "UnknownEntity";
    case AccessError::NilGuid: return "NilGuid";
    case AccessError::DuplicateGuid: return "DuplicateGuid";
    case AccessError::UnknownComponentType: return "UnknownComponentType";
    case AccessError::ComponentMissing: return "ComponentMissing";
    case AccessError::ComponentAlreadyPresent: return "ComponentAlreadyPresent";
    case AccessError::UnknownField: return "UnknownField";
    case AccessError::FieldNotEditable: return "FieldNotEditable";
    case AccessError::KindMismatch: return "KindMismatch";
    case AccessError::EnumValueOutOfRange: return "EnumValueOutOfRange";
    case AccessError::NonFiniteValue: return "NonFiniteValue";
    case AccessError::LightLimitExceeded: return "LightLimitExceeded";
    case AccessError::ActiveCameraProtected: return "ActiveCameraProtected";
  }
  ATLANTIS_CHECK_MSG(false, "toString(AccessError): unhandled enumerator");
  return "(unrecognized AccessError)";
}

namespace {

// After validation every ECS call below must succeed; one that does not is a
// programming error (for instance, applyPending() called during a query),
// never a client-reachable state.
void requireEcs(bool ok, const char* what) {
  ATLANTIS_CHECK_MSG(ok, what);
}

template <typename T>
constexpr schema::TypeId typeOf() {
  return ecs::componentTypeId<T>();
}

}  // namespace

struct RuntimeWorldAccess::Impl {
  explicit Impl(BakedScene& baked) : scene(&baked) {
    for (const auto& [guid, entity] : baked.entities.entries()) index.emplace(guid, entity);
  }

  BakedScene* scene;
  std::map<EntityGuid, ecs::EntityId> index;  // Spec 0052 ruling Q1 (a)
  std::vector<std::pair<CommandTicket, Command>> pending;
  std::uint64_t lastTicket = 0;
  std::vector<Event> events;
  std::vector<CommandFailure> failures;

  using EntityResult = atlantis::Result<ecs::EntityId, AccessError>;
  using Applied = atlantis::Result<Event, AccessError>;

  [[nodiscard]] ecs::World& world() const { return scene->world; }

  [[nodiscard]] EntityResult lookup(const EntityGuid& guid) const {
    const auto found = index.find(guid);
    if (found == index.end() || !world().isValid(found->second)) return EntityResult::Err(AccessError::UnknownEntity);
    return EntityResult::Ok(found->second);
  }

  [[nodiscard]] bool isActiveCamera(ecs::EntityId entity) const {
    return scene->activeCamera.has_value() && *scene->activeCamera == entity;
  }

  template <typename T>
  [[nodiscard]] bool has(ecs::EntityId entity) const {
    const auto result = world().has<T>(entity);
    requireEcs(result.isOk(), "RuntimeWorldAccess: has() failed on a validated entity");
    return result.value();
  }

  // Ruling Q6 / Plan 0052 J1: the lights extraction sees -- entities holding
  // both Light and WorldMatrix -- of `kind`, other than `except`.
  [[nodiscard]] std::uint32_t countLights(LightKind kind, ecs::EntityId except) const {
    std::uint32_t count = 0;
    world().query<const Light, const WorldMatrix>([&](ecs::EntityId id, const Light& light, const WorldMatrix&) {
      if (!(id == except) && light.kind == kind) ++count;
    });
    return count;
  }

  // Whether `entity`, holding a Light of `kind` together with a WorldMatrix,
  // keeps extraction within its limits.
  [[nodiscard]] bool lightFits(LightKind kind, ecs::EntityId entity) const {
    const std::uint32_t limit = kind == LightKind::Directional ? kMaxDirectionalLights : kMaxPointLights;
    return countLights(kind, entity) + 1 <= limit;
  }

  [[nodiscard]] Applied apply(const CreateEntity& command) {
    if (command.entity == EntityGuid{}) return Applied::Err(AccessError::NilGuid);
    if (lookup(command.entity).isOk()) return Applied::Err(AccessError::DuplicateGuid);
    const ecs::EntityId entity = world().createEntity();
    requireEcs(world().isValid(entity), "RuntimeWorldAccess: createEntity() failed");
    index.insert_or_assign(command.entity, entity);
    return Applied::Ok(EntityCreated{command.entity});
  }

  [[nodiscard]] Applied apply(const DestroyEntity& command) {
    const auto entity = lookup(command.entity);
    if (entity.isErr()) return Applied::Err(entity.error());
    if (isActiveCamera(entity.value())) return Applied::Err(AccessError::ActiveCameraProtected);  // J3
    requireEcs(world().destroyEntity(entity.value()).isOk(), "RuntimeWorldAccess: destroyEntity() failed");
    index.erase(command.entity);
    return Applied::Ok(EntityDestroyed{command.entity});
  }

  [[nodiscard]] Applied apply(const AddComponent& command) {
    const auto entity = lookup(command.entity);
    if (entity.isErr()) return Applied::Err(entity.error());
    std::optional<AccessError> refused;
    const bool known = detail::visitComponentType(command.component, [&]<typename T>(std::type_identity<T>) {
      const ecs::EntityId id = entity.value();
      if (has<T>(id)) {
        refused = AccessError::ComponentAlreadyPresent;
        return;
      }
      // J1: adding the second half of a Light + WorldMatrix pair makes the
      // entity visible to extraction; check its kind still fits.
      if constexpr (std::is_same_v<T, Light>) {
        if (has<WorldMatrix>(id) && !lightFits(Light{}.kind, id)) refused = AccessError::LightLimitExceeded;
      } else if constexpr (std::is_same_v<T, WorldMatrix>) {
        if (has<Light>(id)) {
          const auto light = world().get<Light>(id);
          requireEcs(light.isOk(), "RuntimeWorldAccess: get<Light>() failed");
          if (!lightFits(light.value().kind, id)) refused = AccessError::LightLimitExceeded;
        }
      }
      if (refused) return;
      requireEcs(world().add<T>(id, T{}).isOk(), "RuntimeWorldAccess: add() failed");
    });
    if (!known) return Applied::Err(AccessError::UnknownComponentType);
    if (refused) return Applied::Err(*refused);
    return Applied::Ok(ComponentAdded{command.entity, command.component});
  }

  [[nodiscard]] Applied apply(const RemoveComponent& command) {
    const auto entity = lookup(command.entity);
    if (entity.isErr()) return Applied::Err(entity.error());
    std::optional<AccessError> refused;
    const bool known = detail::visitComponentType(command.component, [&]<typename T>(std::type_identity<T>) {
      const ecs::EntityId id = entity.value();
      if (!has<T>(id)) {
        refused = AccessError::ComponentMissing;
        return;
      }
      if constexpr (std::is_same_v<T, Camera> || std::is_same_v<T, WorldMatrix>) {
        if (isActiveCamera(id)) {  // J3
          refused = AccessError::ActiveCameraProtected;
          return;
        }
      }
      requireEcs(world().remove<T>(id).isOk(), "RuntimeWorldAccess: remove() failed");
    });
    if (!known) return Applied::Err(AccessError::UnknownComponentType);
    if (refused) return Applied::Err(*refused);
    return Applied::Ok(ComponentRemoved{command.entity, command.component});
  }

  [[nodiscard]] Applied apply(const SetProperty& command) {
    const PropertyAddress& address = command.address;
    const auto entity = lookup(address.entity);
    if (entity.isErr()) return Applied::Err(entity.error());
    const auto resolved = detail::resolveField(address.component, address.field);
    if (resolved.isErr()) return Applied::Err(resolved.error());
    std::optional<AccessError> refused;
    detail::visitComponentType(address.component, [&]<typename T>(std::type_identity<T>) {
      const ecs::EntityId id = entity.value();
      if (!has<T>(id)) {
        refused = AccessError::ComponentMissing;
        return;
      }
      if (const auto valid = detail::checkValue(resolved.value(), command.value); valid.isErr()) {
        refused = valid.error();
        return;
      }
      auto current = world().get<T>(id);
      requireEcs(current.isOk(), "RuntimeWorldAccess: get() failed on a present component");
      T value = current.value();
      // J1: changing Light.kind on a light extraction sees.
      if constexpr (std::is_same_v<T, Light>) {
        if (address.field == schema::fieldId("world::Light", "kind") && has<WorldMatrix>(id)) {
          const auto kind = static_cast<LightKind>(std::get<EnumValue>(command.value).value);
          if (kind != value.kind && !lightFits(kind, id)) {
            refused = AccessError::LightLimitExceeded;
            return;
          }
        }
      }
      detail::writeField(reinterpret_cast<std::byte*>(&value), address.component, address.field, resolved.value(),
                         command.value);
      requireEcs(world().set<T>(id, value).isOk(), "RuntimeWorldAccess: set() failed on a present component");
    });
    if (refused) return Applied::Err(*refused);
    return Applied::Ok(PropertyChanged{address, command.value});
  }
};

RuntimeWorldAccess::RuntimeWorldAccess(BakedScene& scene) : impl_(std::make_unique<Impl>(scene)) {}
RuntimeWorldAccess::~RuntimeWorldAccess() = default;
RuntimeWorldAccess::RuntimeWorldAccess(RuntimeWorldAccess&&) noexcept = default;
RuntimeWorldAccess& RuntimeWorldAccess::operator=(RuntimeWorldAccess&&) noexcept = default;

bool RuntimeWorldAccess::findEntity(const EntityGuid& entity) const { return impl_->lookup(entity).isOk(); }

atlantis::Result<std::vector<schema::TypeId>, AccessError> RuntimeWorldAccess::listComponents(
    const EntityGuid& entity) const {
  using ResultT = atlantis::Result<std::vector<schema::TypeId>, AccessError>;
  const auto id = impl_->lookup(entity);
  if (id.isErr()) return ResultT::Err(id.error());
  std::vector<schema::TypeId> types;
  std::apply(
      [&](auto... tag) {
        ((impl_->has<decltype(tag)>(id.value()) ? types.push_back(ecs::componentTypeId<decltype(tag)>()) : void()),
         ...);
      },
      ecs::WorldComponentTypes{});
  std::sort(types.begin(), types.end());
  return ResultT::Ok(std::move(types));
}

atlantis::Result<PropertyValue, AccessError> RuntimeWorldAccess::getProperty(const PropertyAddress& address) const {
  using ResultT = atlantis::Result<PropertyValue, AccessError>;
  const auto id = impl_->lookup(address.entity);
  if (id.isErr()) return ResultT::Err(id.error());
  const auto resolved = detail::resolveField(address.component, address.field);
  if (resolved.isErr()) return ResultT::Err(resolved.error());
  std::optional<PropertyValue> value;
  detail::visitComponentType(address.component, [&]<typename T>(std::type_identity<T>) {
    const auto component = impl_->world().get<T>(id.value());
    if (component.isErr()) return;  // ComponentMissing
    const T copy = component.value();
    value = detail::readField(reinterpret_cast<const std::byte*>(&copy), address.component, address.field,
                              resolved.value());
  });
  if (!value) return ResultT::Err(AccessError::ComponentMissing);
  return ResultT::Ok(std::move(*value));
}

CommandTicket RuntimeWorldAccess::submit(Command command) {
  const CommandTicket ticket{++impl_->lastTicket};
  impl_->pending.emplace_back(ticket, std::move(command));
  return ticket;
}

ApplyReport RuntimeWorldAccess::applyPending() {
  ApplyReport report;
  auto pending = std::exchange(impl_->pending, {});
  for (const auto& [ticket, command] : pending) {
    auto applied = std::visit([&](const auto& c) { return impl_->apply(c); }, command);
    if (applied.isOk()) {
      impl_->events.push_back(std::move(applied.value()));
      ++report.applied;
    } else {
      const CommandFailure failure{ticket, applied.error()};
      impl_->failures.push_back(failure);
      report.failures.push_back(failure);
    }
  }
  return report;
}

std::vector<Event> RuntimeWorldAccess::drainEvents() { return std::exchange(impl_->events, {}); }

std::vector<CommandFailure> RuntimeWorldAccess::drainFailures() { return std::exchange(impl_->failures, {}); }

}  // namespace atlantis::world::access

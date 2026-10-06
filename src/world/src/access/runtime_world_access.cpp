#include <atlantis/world/access/runtime_world_access.h>

#include "property_access.h"

#include <atlantis/assert.h>
#include <atlantis/world/ecs/entity_guid_map.h>
#include <atlantis/world/ecs/world.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/light.h>
#include <atlantis/world/scene_instantiation.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
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

// Plan 0053 P3 (Spec 0053 R7; ruling Q7, ADR-0104 D3): Spec 0052's command
// checks, written once, over a state view. They are instantiated for the
// real world (the GUID index plus the ECS) and for a transaction's projected
// state, so projection and execution run the same rule code. The rules see
// only GUIDs, schema ids and values; the order of the checks is Spec 0052's
// (pinned by world_access_precedence_tests.cpp). Kept in this file: no World
// header other than runtime_world_access.h may name a GUID (AGENTS.md).
namespace detail {

// The state Spec 0052's checks read, keyed by GUID (Plan 0053 J2).
template <typename V>
concept WorldStateView = requires(const V& view, const atlantis::asset_system::EntityGuid& entity,
                                  schema::TypeId component, LightKind kind) {
  // A live entity has that GUID.
  { view.exists(entity) } -> std::same_as<bool>;
  // Requires exists(entity) and a World component type.
  { view.has(entity, component) } -> std::same_as<bool>;
  // Requires has(entity, Light).
  { view.lightKind(entity) } -> std::same_as<LightKind>;
  { view.isActiveCamera(entity) } -> std::same_as<bool>;
  // Entities holding Light and WorldMatrix whose Light is of `kind`, other
  // than `entity` (Spec 0052 Correction J1). Requires exists(entity).
  { view.lightCount(kind, entity) } -> std::same_as<std::uint32_t>;
};

constexpr schema::TypeId kCameraType = ecs::componentTypeId<Camera>();
constexpr schema::TypeId kLightType = ecs::componentTypeId<Light>();
constexpr schema::TypeId kWorldMatrixType = ecs::componentTypeId<WorldMatrix>();
constexpr schema::FieldId kLightKindField = schema::fieldId("world::Light", "kind");

[[nodiscard]] bool isWorldComponent(schema::TypeId component) {
  return visitComponentType(component, []<typename T>(std::type_identity<T>) {});
}

// Ruling Q6 (Spec 0052): whether one more light of `kind`, held by `entity`,
// keeps extraction within its limits.
template <WorldStateView V>
[[nodiscard]] bool lightFits(const V& view, LightKind kind, const atlantis::asset_system::EntityGuid& entity) {
  const std::uint32_t limit = kind == LightKind::Directional ? kMaxDirectionalLights : kMaxPointLights;
  return view.lightCount(kind, entity) + 1 <= limit;
}

template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const CreateEntity& command) {
  if (command.entity == atlantis::asset_system::EntityGuid{}) return AccessError::NilGuid;
  if (view.exists(command.entity)) return AccessError::DuplicateGuid;
  return std::nullopt;
}

template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const DestroyEntity& command) {
  if (!view.exists(command.entity)) return AccessError::UnknownEntity;
  if (view.isActiveCamera(command.entity)) return AccessError::ActiveCameraProtected;  // J3
  return std::nullopt;
}

template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const AddComponent& command) {
  const auto& entity = command.entity;
  if (!view.exists(entity)) return AccessError::UnknownEntity;
  if (!isWorldComponent(command.component)) return AccessError::UnknownComponentType;
  if (view.has(entity, command.component)) return AccessError::ComponentAlreadyPresent;
  // J1: adding the second half of a Light + WorldMatrix pair makes the
  // entity visible to extraction; check its kind still fits.
  if (command.component == kLightType && view.has(entity, kWorldMatrixType) &&
      !lightFits(view, Light{}.kind, entity)) {
    return AccessError::LightLimitExceeded;
  }
  if (command.component == kWorldMatrixType && view.has(entity, kLightType) &&
      !lightFits(view, view.lightKind(entity), entity)) {
    return AccessError::LightLimitExceeded;
  }
  return std::nullopt;
}

template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const RemoveComponent& command) {
  const auto& entity = command.entity;
  if (!view.exists(entity)) return AccessError::UnknownEntity;
  if (!isWorldComponent(command.component)) return AccessError::UnknownComponentType;
  if (!view.has(entity, command.component)) return AccessError::ComponentMissing;
  if ((command.component == kCameraType || command.component == kWorldMatrixType) && view.isActiveCamera(entity)) {
    return AccessError::ActiveCameraProtected;  // J3
  }
  return std::nullopt;
}

template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const SetProperty& command) {
  const PropertyAddress& address = command.address;
  if (!view.exists(address.entity)) return AccessError::UnknownEntity;
  const auto resolved = resolveField(address.component, address.field);
  if (resolved.isErr()) return resolved.error();
  if (!view.has(address.entity, address.component)) return AccessError::ComponentMissing;
  if (const auto valid = checkValue(resolved.value(), command.value); valid.isErr()) return valid.error();
  // J1: changing Light.kind on a light extraction sees.
  if (address.component == kLightType && address.field == kLightKindField &&
      view.has(address.entity, kWorldMatrixType)) {
    const auto kind = static_cast<LightKind>(std::get<EnumValue>(command.value).value);
    if (kind != view.lightKind(address.entity) && !lightFits(view, kind, address.entity)) {
      return AccessError::LightLimitExceeded;
    }
  }
  return std::nullopt;
}

// The refusal Spec 0052 gives `command` against `view`, or nullopt when it
// would be applied.
template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const Command& command) {
  return std::visit([&](const auto& c) { return checkCommand(view, c); }, command);
}

}  // namespace detail

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

  [[nodiscard]] ecs::World& world() const { return scene->world; }

  [[nodiscard]] EntityResult lookup(const EntityGuid& guid) const {
    const auto found = index.find(guid);
    if (found == index.end() || !world().isValid(found->second)) return EntityResult::Err(AccessError::UnknownEntity);
    return EntityResult::Ok(found->second);
  }

  // An entity the checks have already found; anything else is a programming
  // error.
  [[nodiscard]] ecs::EntityId validated(const EntityGuid& guid) const {
    const auto entity = lookup(guid);
    requireEcs(entity.isOk(), "RuntimeWorldAccess: a validated GUID names no live entity");
    return entity.value();
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

  // Plan 0053 P3: the real world as Spec 0052's checks see it -- the GUID
  // index plus the ECS. Models detail::WorldStateView.
  struct RealWorldState {
    const Impl* impl;

    [[nodiscard]] bool exists(const EntityGuid& entity) const { return impl->lookup(entity).isOk(); }
    [[nodiscard]] bool has(const EntityGuid& entity, schema::TypeId component) const {
      const ecs::EntityId id = impl->validated(entity);
      bool present = false;
      detail::visitComponentType(component, [&]<typename T>(std::type_identity<T>) { present = impl->has<T>(id); });
      return present;
    }
    [[nodiscard]] LightKind lightKind(const EntityGuid& entity) const {
      const auto light = impl->world().get<Light>(impl->validated(entity));
      requireEcs(light.isOk(), "RuntimeWorldAccess: get<Light>() failed");
      return light.value().kind;
    }
    [[nodiscard]] bool isActiveCamera(const EntityGuid& entity) const {
      const auto id = impl->lookup(entity);
      return id.isOk() && impl->isActiveCamera(id.value());
    }
    [[nodiscard]] std::uint32_t lightCount(LightKind kind, const EntityGuid& entity) const {
      return impl->countLights(kind, impl->validated(entity));
    }
  };
  static_assert(detail::WorldStateView<RealWorldState>);

  [[nodiscard]] RealWorldState real() const { return RealWorldState{this}; }

  // The executors: a command's ECS lowering, index update and event, run
  // only after checkCommand() passed for it against the real world (every
  // ECS call below must then succeed).
  Event execute(const CreateEntity& command) {
    const ecs::EntityId entity = world().createEntity();
    requireEcs(world().isValid(entity), "RuntimeWorldAccess: createEntity() failed");
    index.insert_or_assign(command.entity, entity);
    return EntityCreated{command.entity};
  }

  Event execute(const DestroyEntity& command) {
    requireEcs(world().destroyEntity(validated(command.entity)).isOk(), "RuntimeWorldAccess: destroyEntity() failed");
    index.erase(command.entity);
    return EntityDestroyed{command.entity};
  }

  Event execute(const AddComponent& command) {
    const ecs::EntityId id = validated(command.entity);
    detail::visitComponentType(command.component, [&]<typename T>(std::type_identity<T>) {
      requireEcs(world().add<T>(id, T{}).isOk(), "RuntimeWorldAccess: add() failed");
    });
    return ComponentAdded{command.entity, command.component};
  }

  Event execute(const RemoveComponent& command) {
    const ecs::EntityId id = validated(command.entity);
    detail::visitComponentType(command.component, [&]<typename T>(std::type_identity<T>) {
      requireEcs(world().remove<T>(id).isOk(), "RuntimeWorldAccess: remove() failed");
    });
    return ComponentRemoved{command.entity, command.component};
  }

  Event execute(const SetProperty& command) {
    const PropertyAddress& address = command.address;
    const ecs::EntityId id = validated(address.entity);
    const auto resolved = detail::resolveField(address.component, address.field);
    requireEcs(resolved.isOk(), "RuntimeWorldAccess: a validated field did not resolve");
    detail::visitComponentType(address.component, [&]<typename T>(std::type_identity<T>) {
      auto current = world().get<T>(id);
      requireEcs(current.isOk(), "RuntimeWorldAccess: get() failed on a present component");
      T value = current.value();
      detail::writeField(reinterpret_cast<std::byte*>(&value), address.component, address.field, resolved.value(),
                         command.value);
      requireEcs(world().set<T>(id, value).isOk(), "RuntimeWorldAccess: set() failed on a present component");
    });
    return PropertyChanged{address, command.value};
  }

  // Spec 0052's single-command path (ruling Q9; Spec 0053 R6): checked
  // against the world as it is now, then applied, or refused with no effect.
  [[nodiscard]] std::optional<AccessError> applyOne(const Command& command) {
    if (const auto refused = detail::checkCommand(real(), command)) return refused;
    events.push_back(std::visit([&](const auto& c) { return execute(c); }, command));
    return std::nullopt;
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
    if (const auto refused = impl_->applyOne(command)) {
      const CommandFailure failure{ticket, *refused};
      impl_->failures.push_back(failure);
      report.failures.push_back(failure);
    } else {
      ++report.applied;
    }
  }
  return report;
}

std::vector<Event> RuntimeWorldAccess::drainEvents() { return std::exchange(impl_->events, {}); }

std::vector<CommandFailure> RuntimeWorldAccess::drainFailures() { return std::exchange(impl_->failures, {}); }

}  // namespace atlantis::world::access

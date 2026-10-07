#include <atlantis/world/access/runtime_world_access.h>

#include "property_access.h"

#include <atlantis/assert.h>
#include <atlantis/world/ecs/entity_guid_map.h>
#include <atlantis/world/ecs/world.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/light.h>
#include <atlantis/world/scene_instantiation.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <functional>
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

// Plan 0053 P4 (Spec 0053 R2, R3; ruling Q1, ADR-0104 D2): a transaction's
// projected state -- the state the checks above read, as the transaction's
// commands so far would leave it -- so the whole transaction is checked
// before any of it is applied. Nothing here touches the world.

constexpr std::size_t kWorldComponentCount = std::tuple_size_v<ecs::WorldComponentTypes>;

// The position of a World component type in ecs::WorldComponentTypes, or
// nullopt for any other TypeId.
[[nodiscard]] std::optional<std::size_t> worldComponentIndex(schema::TypeId component) {
  std::optional<std::size_t> found;
  [&]<typename... Ts>(std::tuple<Ts...>*) {
    std::size_t position = 0;
    ((!found && ecs::componentTypeId<Ts>() == component ? void(found = position) : void(), ++position), ...);
  }(static_cast<ecs::WorldComponentTypes*>(nullptr));
  return found;
}

// One entity as the checks see it.
struct ShadowEntity {
  bool live = false;
  std::bitset<kWorldComponentCount> components;
  LightKind lightKind = LightKind::Directional;  // when it holds a Light
  bool activeCamera = false;
};

// Light + WorldMatrix holders by LightKind (Directional, Point).
using LightCounts = std::array<std::uint32_t, 2>;

[[nodiscard]] std::size_t lightSlot(LightKind kind) { return static_cast<std::size_t>(kind); }

// `entity` as `view` sees it now.
template <WorldStateView V>
[[nodiscard]] ShadowEntity seedShadowEntity(const V& view, const EntityGuid& entity) {
  ShadowEntity shadow;
  shadow.live = view.exists(entity);
  shadow.activeCamera = view.isActiveCamera(entity);
  if (!shadow.live) return shadow;
  [&]<typename... Ts>(std::tuple<Ts...>*) {
    std::size_t position = 0;
    ((shadow.components[position++] = view.has(entity, ecs::componentTypeId<Ts>())), ...);
  }(static_cast<ecs::WorldComponentTypes*>(nullptr));
  if (view.has(entity, kLightType)) shadow.lightKind = view.lightKind(entity);
  return shadow;
}

// The kind under which `shadow` counts toward the light limits, if it does:
// a live entity holding both Light and WorldMatrix (Spec 0052 Correction J1).
[[nodiscard]] std::optional<LightKind> countedAs(const ShadowEntity& shadow) {
  static const std::size_t light = *worldComponentIndex(kLightType);
  static const std::size_t matrix = *worldComponentIndex(kWorldMatrixType);
  if (!shadow.live || !shadow.components[light] || !shadow.components[matrix]) return std::nullopt;
  return shadow.lightKind;
}

// Models WorldStateView. Entities are seeded lazily, on first touch, from the
// real world, which does not change while a transaction is projected; the
// light counts are the real ones when projection starts, then kept by the
// difference each projected command makes to what it touches (Plan 0053 J4).
class ProjectedWorldState {
 public:
  using Seed = std::function<ShadowEntity(const EntityGuid&)>;

  ProjectedWorldState(Seed seed, LightCounts lightCounts) : seed_(std::move(seed)), lightCounts_(lightCounts) {}

  [[nodiscard]] bool exists(const EntityGuid& guid) const { return entity(guid).live; }
  [[nodiscard]] bool has(const EntityGuid& guid, schema::TypeId component) const {
    const auto position = worldComponentIndex(component);
    ATLANTIS_CHECK_MSG(position.has_value(), "ProjectedWorldState::has(): not a World component type");
    return entity(guid).components[*position];
  }
  [[nodiscard]] LightKind lightKind(const EntityGuid& guid) const { return entity(guid).lightKind; }
  [[nodiscard]] bool isActiveCamera(const EntityGuid& guid) const { return entity(guid).activeCamera; }
  [[nodiscard]] std::uint32_t lightCount(LightKind kind, const EntityGuid& guid) const {
    const std::uint32_t self = countedAs(entity(guid)) == kind ? 1u : 0u;
    return lightCounts_[lightSlot(kind)] - self;
  }

  // Applies `command` to the projection. Requires checkCommand(*this,
  // command) to have passed.
  void project(const Command& command) {
    const EntityGuid& guid = std::visit(
        [](const auto& c) -> const EntityGuid& {
          if constexpr (std::is_same_v<std::decay_t<decltype(c)>, SetProperty>) {
            return c.address.entity;
          } else {
            return c.entity;
          }
        },
        command);
    ShadowEntity& shadow = entity(guid);
    const std::optional<LightKind> before = countedAs(shadow);
    std::visit(
        [&](const auto& c) {
          using C = std::decay_t<decltype(c)>;
          if constexpr (std::is_same_v<C, CreateEntity>) {
            shadow = ShadowEntity{};  // a new entity: no components, never the bake's active camera
            shadow.live = true;
          } else if constexpr (std::is_same_v<C, DestroyEntity>) {
            shadow = ShadowEntity{};
          } else if constexpr (std::is_same_v<C, AddComponent>) {
            shadow.components[*worldComponentIndex(c.component)] = true;
            if (c.component == kLightType) shadow.lightKind = Light{}.kind;
          } else if constexpr (std::is_same_v<C, RemoveComponent>) {
            shadow.components[*worldComponentIndex(c.component)] = false;
          } else {
            // Of all field values, only Light.kind is read by the checks.
            if (c.address.component == kLightType && c.address.field == kLightKindField) {
              shadow.lightKind = static_cast<LightKind>(std::get<EnumValue>(c.value).value);
            }
          }
        },
        command);
    const std::optional<LightKind> after = countedAs(shadow);
    if (before) --lightCounts_[lightSlot(*before)];
    if (after) ++lightCounts_[lightSlot(*after)];
  }

 private:
  [[nodiscard]] ShadowEntity& entity(const EntityGuid& guid) const {
    auto found = entities_.find(guid);
    if (found == entities_.end()) found = entities_.emplace(guid, seed_(guid)).first;
    return found->second;
  }

  Seed seed_;
  LightCounts lightCounts_;
  mutable std::map<EntityGuid, ShadowEntity> entities_;
};
static_assert(WorldStateView<ProjectedWorldState>);

}  // namespace detail

struct RuntimeWorldAccess::Impl {
  explicit Impl(BakedScene& baked) : scene(&baked) {
    for (const auto& [guid, entity] : baked.entities.entries()) index.emplace(guid, entity);
  }

  BakedScene* scene;
  std::map<EntityGuid, ecs::EntityId> index;  // Spec 0052 ruling Q1 (a)
  // A single command, or a transaction's commands (Plan 0053 P5); `first` is
  // the (first) command's ticket.
  struct PendingEntry {
    CommandTicket first;
    std::variant<Command, std::vector<Command>> body;
  };
  std::vector<PendingEntry> pending;
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

  // The Light + WorldMatrix holders of each kind, in one pass.
  [[nodiscard]] detail::LightCounts countAllLights() const {
    detail::LightCounts counts{};
    world().query<const Light, const WorldMatrix>([&](ecs::EntityId, const Light& light, const WorldMatrix&) {
      ++counts[static_cast<std::size_t>(light.kind)];
    });
    return counts;
  }

  // Spec 0053 (Plan 0053 P5; ADR-0104 D2): the whole transaction is checked
  // against its projection first, and nothing is applied unless every
  // command passes -- so an abort never touched the world. Then each command
  // is applied, re-checked against the real world first (J3): a refusal
  // there would mean the projection drifted from the rules, a programming
  // error, never a partial commit.
  [[nodiscard]] std::optional<CommandFailure> applyTransaction(CommandTicket first,
                                                               const std::vector<Command>& commands) {
    const RealWorldState realState = real();
    detail::ProjectedWorldState projected(
        [&realState](const EntityGuid& guid) { return detail::seedShadowEntity(realState, guid); },
        countAllLights());
    for (std::size_t i = 0; i < commands.size(); ++i) {
      if (const auto refused = detail::checkCommand(projected, commands[i])) {
        return CommandFailure{CommandTicket{first.value + i}, *refused};
      }
      projected.project(commands[i]);
    }
    for (const Command& command : commands) {
      ATLANTIS_CHECK_MSG(!detail::checkCommand(real(), command).has_value(),
                         "RuntimeWorldAccess: a transaction's command, accepted by its projection, was refused");
      events.push_back(std::visit([&](const auto& c) { return execute(c); }, command));
    }
    return std::nullopt;
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
  impl_->pending.push_back({ticket, std::variant<Command, std::vector<Command>>(std::in_place_index<0>, std::move(command))});
  return ticket;
}

TransactionTicket RuntimeWorldAccess::submitTransaction(std::vector<Command> commands) {
  if (commands.empty()) return TransactionTicket{};  // ruling Q4 (4d): nothing queued, no ticket
  const TransactionTicket ticket{CommandTicket{impl_->lastTicket + 1}, commands.size()};
  impl_->lastTicket += commands.size();
  impl_->pending.push_back(
      {ticket.first, std::variant<Command, std::vector<Command>>(std::in_place_index<1>, std::move(commands))});
  return ticket;
}

ApplyReport RuntimeWorldAccess::applyPending() {
  ApplyReport report;
  auto pending = std::exchange(impl_->pending, {});
  const auto refuse = [&](const CommandFailure& failure) {
    impl_->failures.push_back(failure);
    report.failures.push_back(failure);
  };
  for (const auto& entry : pending) {
    if (const Command* command = std::get_if<Command>(&entry.body)) {
      if (const auto refused = impl_->applyOne(*command)) {
        refuse(CommandFailure{entry.first, *refused});
      } else {
        ++report.applied;
      }
    } else {
      const auto& commands = std::get<std::vector<Command>>(entry.body);
      if (const auto failure = impl_->applyTransaction(entry.first, commands)) {
        refuse(*failure);
      } else {
        report.applied += commands.size();
      }
    }
  }
  return report;
}

std::vector<Event> RuntimeWorldAccess::drainEvents() { return std::exchange(impl_->events, {}); }

std::vector<CommandFailure> RuntimeWorldAccess::drainFailures() { return std::exchange(impl_->failures, {}); }

}  // namespace atlantis::world::access

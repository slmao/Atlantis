#pragma once

#include <atlantis/gameplay/binding.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Spec 0057 R2/R4/R5, ADR-0110 D2, Plan 0057 P2 (J6): a transaction builder,
// independent of any connection. It records operations in call order and
// resolves nothing; World::submit() resolves it whole -- names through
// connection::text (J5), typed operations through their bindings (R5) -- and
// submits it as one RuntimeConnection transaction, or refuses all of it
// (nothing submitted, no ticket) if any name or binding fails. It never
// reorders, merges or splits operations (R4). A plain value; not
// thread-safe.
namespace atlantis::gameplay {

class Transaction {
 public:
  enum class Kind : std::uint8_t { Create, Destroy, AddComponent, RemoveComponent, SetProperty };

  // What an operation names, in one of three forms: by name (`name`: a type
  // name, or a property path), by id (`component`, `field`), or typed
  // (`bindings` non-empty: the generated table and the type's index, plus
  // the ids).
  struct Target {
    std::string name;
    schema::TypeId component;
    schema::FieldId field;
    std::span<const TypeBinding> bindings;
    std::size_t bindingIndex = 0;
  };

  struct Operation {
    Kind kind = Kind::Create;
    EntityGuid entity;
    Target target;
    PropertyValue value = ::atlantis::world::access::Absent{};  // SetProperty
  };

  Transaction& create(const EntityGuid& entity);
  Transaction& destroy(const EntityGuid& entity);

  // A bare AddComponent: the component with World's default values.
  Transaction& add(const EntityGuid& entity, std::string_view type);
  Transaction& add(const EntityGuid& entity, schema::TypeId type);
  // AddComponent, then one SetProperty per given leaf, in the given order;
  // leaf paths are relative to the type ("kind", "fog.density").
  Transaction& add(const EntityGuid& entity, std::string_view type,
                   const std::vector<std::pair<std::string, PropertyValue>>& leaves);

  Transaction& remove(const EntityGuid& entity, std::string_view type);
  Transaction& remove(const EntityGuid& entity, schema::TypeId type);

  Transaction& set(const EntityGuid& entity, std::string_view path, PropertyValue value);
  Transaction& set(const PropertyAddress& address, PropertyValue value);

  // --- Typed (Spec 0057 R3, R10; ADR-0111 D3): checked against the
  // connection's schema at World::submit (R5).
  // AddComponent, then one SetProperty per leaf in the schema's leaf order.
  // Only for a type whose every leaf is Editable (R10): for another, add the
  // bare component and set its editable leaves. Every leaf is written, so a
  // value-initialized C{} writes zeros and enum value 0 (which is a declared
  // constant only where the enum's binding says zeroIsDeclared) -- not
  // World's defaults; add<C>(entity) gives World's defaults.
  template <Component C>
    requires(Codec<C>::allEditable)
  Transaction& add(const EntityGuid& entity, const C& value) {
    add<C>(entity);
    std::array<PropertyValue, Codec<C>::leaves.size()> values{};
    Codec<C>::write(value, values);
    for (std::size_t i = 0; i < values.size(); ++i) {
      operations_.push_back(Operation{Kind::SetProperty, entity, typedTarget<C>(Codec<C>::leaves[i]), values[i]});
    }
    return *this;
  }
  // A bare AddComponent: the component with World's default values.
  template <Component C>
  Transaction& add(const EntityGuid& entity) {
    operations_.push_back(Operation{Kind::AddComponent, entity, typedTarget<C>({}), ::atlantis::world::access::Absent{}});
    return *this;
  }
  template <Component C>
  Transaction& remove(const EntityGuid& entity) {
    operations_.push_back(
        Operation{Kind::RemoveComponent, entity, typedTarget<C>({}), ::atlantis::world::access::Absent{}});
    return *this;
  }
  template <Bound C, class T>
  Transaction& set(const EntityGuid& entity, const Field<C, T>& handle, const std::type_identity_t<T>& value) {
    operations_.push_back(Operation{Kind::SetProperty, entity, typedTarget<C>(handle.field), toPropertyValue(value)});
    return *this;
  }

  [[nodiscard]] const std::vector<Operation>& operations() const noexcept { return operations_; }
  [[nodiscard]] bool empty() const noexcept { return operations_.empty(); }

 private:
  template <Bound C>
  [[nodiscard]] static Target typedTarget(schema::FieldId field) {
    Target target;
    target.component = BindingOf<C>::table[BindingOf<C>::index].id;
    target.field = field;
    target.bindings = BindingOf<C>::table;
    target.bindingIndex = BindingOf<C>::index;
    return target;
  }

  std::vector<Operation> operations_;
};

// Failures drained from a connection, kept so a client can ask about any of
// its tickets later (Spec 0052 J4 / Spec 0053: one failure per refused
// command or aborted transaction, routed by ticket). A plain value.
class FailureLog {
 public:
  void absorb(std::vector<::atlantis::world::access::CommandFailure> failures);
  [[nodiscard]] std::optional<::atlantis::world::access::CommandFailure> refusal(
      ::atlantis::world::access::CommandTicket ticket) const;
  // The failure whose ticket lies in the transaction's range: it was aborted
  // at that command, and none of it applied.
  [[nodiscard]] std::optional<::atlantis::world::access::CommandFailure> refusal(
      ::atlantis::world::access::TransactionTicket ticket) const;
  [[nodiscard]] const std::vector<::atlantis::world::access::CommandFailure>& failures() const noexcept {
    return failures_;
  }

 private:
  std::vector<::atlantis::world::access::CommandFailure> failures_;
};

}  // namespace atlantis::gameplay

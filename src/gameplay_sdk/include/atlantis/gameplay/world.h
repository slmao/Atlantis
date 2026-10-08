#pragma once

#include <atlantis/gameplay/binding.h>
#include <atlantis/gameplay/error.h>
#include <atlantis/gameplay/query_batch.h>
#include <atlantis/gameplay/subscription.h>
#include <atlantis/gameplay/transaction.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Spec 0057 R2/R4, ADR-0110 D2, Plan 0057 P2: the Gameplay SDK's reflective
// layer -- types and fields by name (connection::text's grammar, J5) or id,
// component-level reads, the component filter, transactions, subscriptions
// -- over one borrowed RuntimeConnection. Every operation is a sequence of
// RuntimeConnection calls a client could make by hand, in the order the
// client wrote it; the SDK adds no rule the boundary owns (finiteness,
// limits, editability, enum ranges stay the boundary's refusals, R4).
//
// A query the World refuses returns Error{Refused}; a command the World
// refuses is not an error here -- it is applied (or refused) at the
// owner's next frame and reported by ticket (drainFailures(), FailureLog),
// exactly as RuntimeConnection reports it.
//
// Ownership: borrows the connection and the optional QueryBatch; both must
// outlive the World. Not thread-safe: the connection's thread, between the
// owner's frames in process (ADR-0004, ADR-0105), the client's own thread
// over Remote (ADR-0106).
namespace atlantis::gameplay {

struct DynamicLeaf {
  std::string path;  // canonical, "Camera.fog.density"
  schema::FieldId field;
  PropertyValue value;
  friend bool operator==(const DynamicLeaf&, const DynamicLeaf&) = default;
};

// A component's leaves, in connection::text::leavesOf order, with values.
// Not a snapshot: each leaf is as it was when it was answered (R10).
struct DynamicComponent {
  schema::TypeId type;
  std::string name;  // qualified, "world::Light"
  std::vector<DynamicLeaf> leaves;
};

class World {
 public:
  // `batch` serves component reads and the component filter; when null, a
  // SequentialQueryBatch over `connection` (one query at a time) does.
  explicit World(::atlantis::connection::RuntimeConnection& connection, QueryBatch* batch = nullptr);
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  [[nodiscard]] ::atlantis::connection::RuntimeConnection& connection() const noexcept { return connection_; }
  [[nodiscard]] std::span<const schema::TypeDescriptor> schema() const { return connection_.schema(); }

  // --- Queries.
  [[nodiscard]] bool exists(const EntityGuid& entity) const;
  [[nodiscard]] std::vector<EntityGuid> entities() const;
  [[nodiscard]] atlantis::Result<std::vector<schema::TypeId>, Error> components(const EntityGuid& entity) const;
  // Every listed entity holding all of `types` (short or qualified names),
  // in listEntities() order: one listEntities() and one batched
  // listComponents for all of them, filtered here.
  [[nodiscard]] atlantis::Result<std::vector<EntityGuid>, Error> entitiesWith(
      std::initializer_list<std::string_view> types);
  [[nodiscard]] atlantis::Result<std::vector<EntityGuid>, Error> entitiesWith(std::span<const schema::TypeId> types);

  // --- Properties, by path ("Light.intensity", "Camera.fog.density") or id.
  [[nodiscard]] atlantis::Result<PropertyAddress, Error> resolve(const EntityGuid& entity,
                                                                 std::string_view path) const;
  [[nodiscard]] atlantis::Result<PropertyValue, Error> get(const EntityGuid& entity, std::string_view path) const;
  [[nodiscard]] atlantis::Result<PropertyValue, Error> get(const PropertyAddress& address) const;
  // One SetProperty, submitted alone.
  atlantis::Result<::atlantis::world::access::CommandTicket, Error> set(const EntityGuid& entity,
                                                                       std::string_view path, PropertyValue value);

  // --- Components: every leaf in one batched getProperties.
  [[nodiscard]] atlantis::Result<DynamicComponent, Error> readComponent(const EntityGuid& entity,
                                                                        std::string_view type) const;

  // --- Submission. A transaction is resolved whole: any error refuses all
  // of it, nothing is submitted and no ticket is taken (J6). Otherwise it is
  // exactly one submitTransaction() of its operations, in order.
  atlantis::Result<::atlantis::world::access::TransactionTicket, Error> submit(const Transaction& transaction);
  // One command, passed through unchanged.
  ::atlantis::world::access::CommandTicket submit(::atlantis::world::access::Command command);

  // --- Events and outcomes.
  [[nodiscard]] Subscription subscribe(::atlantis::connection::EventFilter filter = {});
  [[nodiscard]] std::vector<::atlantis::world::access::CommandFailure> drainFailures();

  // --- Typed (Spec 0057 R3, R5, R10; ADR-0111 D3, D4). Every typed call
  // first checks its type's generated binding -- and, recursively, every
  // struct or enum it references -- against the connection's schema (once
  // per type, cached for this World's lifetime); on a mismatch it returns
  // SchemaMismatch and submits nothing. The reflective calls above are never
  // blocked by a binding.
  [[nodiscard]] atlantis::Result<std::monostate, Error> checkBinding(std::span<const TypeBinding> bindings,
                                                                     std::size_t index) const;
  template <Bound T>
  [[nodiscard]] atlantis::Result<std::monostate, Error> checkCompatible() const {
    return checkBinding(BindingOf<T>::table, BindingOf<T>::index);
  }

  template <Bound C, class T, Access A>
  [[nodiscard]] atlantis::Result<T, Error> get(const EntityGuid& entity, const FieldHandle<C, T, A>& handle) const;
  // One SetProperty, submitted alone. A ReadOnlyField, or a value not
  // convertible to the leaf's C++ type, does not compile.
  template <Bound C, class T>
  atlantis::Result<::atlantis::world::access::CommandTicket, Error> set(const EntityGuid& entity,
                                                                       const Field<C, T>& handle,
                                                                       const std::type_identity_t<T>& value);
  // Every leaf of C in one batched getProperties. Not a snapshot (R10): each
  // leaf is as it was when answered. A read is consistent only if, for its
  // whole duration, the Runtime stays paused, no client calls step or
  // resume, and no step is pending; nothing here enforces that.
  template <Component C>
  [[nodiscard]] atlantis::Result<C, Error> read(const EntityGuid& entity) const;
  template <Component... C>
    requires(sizeof...(C) > 0)
  [[nodiscard]] atlantis::Result<std::vector<EntityGuid>, Error> entitiesWith();
  // The handle's value if `event` is a PropertyChanged of that property;
  // empty otherwise.
  template <Bound C, class T, Access A>
  [[nodiscard]] atlantis::Result<std::optional<T>, Error> decode(const ::atlantis::world::access::Event& event,
                                                                 const FieldHandle<C, T, A>& handle) const;

 private:
  [[nodiscard]] atlantis::Result<schema::TypeId, Error> typeNamed(std::string_view type) const;
  [[nodiscard]] atlantis::Result<::atlantis::world::access::Command, Error> resolve(
      const Transaction::Operation& operation);

  ::atlantis::connection::RuntimeConnection& connection_;
  std::unique_ptr<SequentialQueryBatch> sequential_;
  QueryBatch* batch_ = nullptr;
  mutable std::map<std::uint64_t, bool> compatible_;  // TypeId -> the binding matched (R5)
};

template <Bound C, class T, Access A>
atlantis::Result<T, Error> World::get(const EntityGuid& entity, const FieldHandle<C, T, A>& handle) const {
  using ResultT = atlantis::Result<T, Error>;
  if (auto compatible = checkCompatible<C>(); compatible.isErr()) return ResultT::Err(std::move(compatible.error()));
  auto value = connection_.getProperty(PropertyAddress{entity, handle.component, handle.field});
  if (value.isErr()) return ResultT::Err(Error::refused(value.error(), std::string(handle.path)));
  T out{};
  if (!fromPropertyValue(value.value(), out)) return ResultT::Err(Error::mismatch(std::string(handle.path)));
  return ResultT::Ok(std::move(out));
}

template <Bound C, class T>
atlantis::Result<::atlantis::world::access::CommandTicket, Error> World::set(const EntityGuid& entity,
                                                                            const Field<C, T>& handle,
                                                                            const std::type_identity_t<T>& value) {
  using ResultT = atlantis::Result<::atlantis::world::access::CommandTicket, Error>;
  if (auto compatible = checkCompatible<C>(); compatible.isErr()) return ResultT::Err(std::move(compatible.error()));
  return ResultT::Ok(connection_.submit(::atlantis::world::access::SetProperty{
      PropertyAddress{entity, handle.component, handle.field}, toPropertyValue(value)}));
}

template <Component C>
atlantis::Result<C, Error> World::read(const EntityGuid& entity) const {
  using ResultT = atlantis::Result<C, Error>;
  if (auto compatible = checkCompatible<C>(); compatible.isErr()) return ResultT::Err(std::move(compatible.error()));
  const TypeBinding& binding = BindingOf<C>::table[BindingOf<C>::index];
  std::vector<PropertyAddress> addresses;
  addresses.reserve(Codec<C>::leaves.size());
  for (const schema::FieldId field : Codec<C>::leaves) addresses.push_back(PropertyAddress{entity, binding.id, field});
  auto answers = batch_->getProperties(addresses);
  std::array<PropertyValue, Codec<C>::leaves.size()> values{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i >= answers.size()) {
      return ResultT::Err(Error::refused(::atlantis::world::access::AccessError::UnknownEntity,
                                         std::string(binding.name)));
    }
    if (answers[i].isErr()) return ResultT::Err(Error::refused(answers[i].error(), std::string(binding.name)));
    values[i] = std::move(answers[i].value());
  }
  C out{};
  if (!Codec<C>::read(values, out)) return ResultT::Err(Error::mismatch(std::string(binding.name)));
  return ResultT::Ok(std::move(out));
}

template <Component... C>
  requires(sizeof...(C) > 0)
atlantis::Result<std::vector<EntityGuid>, Error> World::entitiesWith() {
  using ResultT = atlantis::Result<std::vector<EntityGuid>, Error>;
  std::optional<Error> failure;
  const auto check = [&](atlantis::Result<std::monostate, Error> compatible) {
    if (!failure.has_value() && compatible.isErr()) failure = compatible.error();
  };
  (check(checkCompatible<C>()), ...);
  if (failure.has_value()) return ResultT::Err(std::move(*failure));
  const std::array<schema::TypeId, sizeof...(C)> ids{BindingOf<C>::table[BindingOf<C>::index].id...};
  return entitiesWith(std::span<const schema::TypeId>(ids));
}

template <Bound C, class T, Access A>
atlantis::Result<std::optional<T>, Error> World::decode(const ::atlantis::world::access::Event& event,
                                                        const FieldHandle<C, T, A>& handle) const {
  using ResultT = atlantis::Result<std::optional<T>, Error>;
  if (auto compatible = checkCompatible<C>(); compatible.isErr()) return ResultT::Err(std::move(compatible.error()));
  const auto* changed = std::get_if<::atlantis::world::access::PropertyChanged>(&event);
  if (changed == nullptr || changed->address.component != handle.component || changed->address.field != handle.field) {
    return ResultT::Ok(std::nullopt);
  }
  T value{};
  if (!fromPropertyValue(changed->value, value)) return ResultT::Err(Error::mismatch(std::string(handle.path)));
  return ResultT::Ok(std::optional<T>(std::move(value)));
}

}  // namespace atlantis::gameplay

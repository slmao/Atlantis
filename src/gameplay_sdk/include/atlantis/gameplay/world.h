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

#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

 private:
  [[nodiscard]] atlantis::Result<schema::TypeId, Error> typeNamed(std::string_view type) const;
  [[nodiscard]] atlantis::Result<::atlantis::world::access::Command, Error> resolve(
      const Transaction::Operation& operation);

  ::atlantis::connection::RuntimeConnection& connection_;
  std::unique_ptr<SequentialQueryBatch> sequential_;
  QueryBatch* batch_ = nullptr;
};

}  // namespace atlantis::gameplay

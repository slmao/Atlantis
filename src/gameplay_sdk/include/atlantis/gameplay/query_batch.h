#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <span>
#include <vector>

// Spec 0057 R2/R10, ADR-0110 D2, Plan 0057 P5 (ruling Q9): many queries at
// once, transport-neutral and owned by the SDK -- the shape of the CLI's
// cli::QueryBatch (Plan 0055 P9). The SDK uses it for exactly two things: a
// component read (one getProperties for all its leaves) and the component
// filter (one listComponents for every listed entity). A Remote client
// supplies an adapter over RemoteSession's pipelined queries; the SDK never
// names Remote.
//
// Results come back in input order. That is all a batch promises: not one
// instant (the default asks one at a time; an adapter may split), so a
// multi-leaf read is not a snapshot (R10). Not thread-safe: the connection's
// thread (ADR-0004).
namespace atlantis::gameplay {

class QueryBatch {
 public:
  virtual ~QueryBatch() = default;
  [[nodiscard]] virtual std::vector<atlantis::Result<std::vector<schema::TypeId>, ::atlantis::world::access::AccessError>>
  listComponents(std::span<const ::atlantis::asset_system::EntityGuid> entities) = 0;
  [[nodiscard]] virtual std::vector<
      atlantis::Result<::atlantis::world::access::PropertyValue, ::atlantis::world::access::AccessError>>
  getProperties(std::span<const ::atlantis::world::access::PropertyAddress> addresses) = 0;
};

// The default: one query at a time over the connection. Borrows it.
class SequentialQueryBatch final : public QueryBatch {
 public:
  explicit SequentialQueryBatch(::atlantis::connection::RuntimeConnection& connection) : connection_(connection) {}
  [[nodiscard]] std::vector<atlantis::Result<std::vector<schema::TypeId>, ::atlantis::world::access::AccessError>>
  listComponents(std::span<const ::atlantis::asset_system::EntityGuid> entities) override;
  [[nodiscard]] std::vector<
      atlantis::Result<::atlantis::world::access::PropertyValue, ::atlantis::world::access::AccessError>>
  getProperties(std::span<const ::atlantis::world::access::PropertyAddress> addresses) override;

 private:
  ::atlantis::connection::RuntimeConnection& connection_;
};

}  // namespace atlantis::gameplay

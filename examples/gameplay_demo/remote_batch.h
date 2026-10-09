#pragma once

#include <atlantis/gameplay/query_batch.h>
#include <atlantis/remote/remote_client.h>

#include <span>
#include <vector>

// Plan 0057 P5 (Spec 0057 ruling Q9; ADR-0110 D2): the example's QueryBatch
// over RemoteSession's pipelined queries -- the adapter the `atlantis`
// executable writes for cli::QueryBatch (src/cli/app/main.cpp). The SDK
// never names Remote; the client that chose the transport adapts it.
// Borrows the session. Not thread-safe.
namespace atlantis::examples::gameplay_demo {

class RemoteBatch final : public atlantis::gameplay::QueryBatch {
 public:
  explicit RemoteBatch(atlantis::remote::RemoteSession& session) : session_(session) {}
  std::vector<atlantis::Result<std::vector<atlantis::schema::TypeId>, atlantis::world::access::AccessError>>
  listComponents(std::span<const atlantis::asset_system::EntityGuid> entities) override {
    return session_.listComponents(entities);
  }
  std::vector<atlantis::Result<atlantis::world::access::PropertyValue, atlantis::world::access::AccessError>>
  getProperties(std::span<const atlantis::world::access::PropertyAddress> addresses) override {
    return session_.getProperties(addresses);
  }

 private:
  atlantis::remote::RemoteSession& session_;
};

}  // namespace atlantis::examples::gameplay_demo

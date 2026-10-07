#include <atlantis/connection/in_process_endpoint.h>

#include <atlantis/assert.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/world_schema.h>

#include <cstdint>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace atlantis::connection {

namespace access = atlantis::world::access;
using atlantis::asset_system::EntityGuid;

namespace {

struct Subscription {
  EventFilter filter;
  std::vector<access::Event> events;
};

struct ConnectionState {
  std::map<std::uint64_t, Subscription> subscriptions;  // by SubscriptionId value
  std::vector<access::CommandFailure> failures;
};

// The tickets one submission took: [first, last], and who submitted them.
struct TicketRange {
  std::uint64_t last = 0;
  std::uint64_t connection = 0;
};

}  // namespace

struct InProcessEndpoint::State {
  access::RuntimeWorldAccess* boundary = nullptr;
  std::uint64_t nextConnection = 1;
  std::uint64_t nextSubscription = 1;
  std::map<std::uint64_t, ConnectionState> connections;  // by connection key
  std::map<std::uint64_t, TicketRange> tickets;          // by first ticket

  // Plan 0054 P4 / J3: move the boundary's queues out once and distribute.
  void pump() {
    for (access::Event& event : boundary->drainEvents()) {
      for (auto& [key, connection] : connections) {
        for (auto& [id, subscription] : connection.subscriptions) {
          if (subscription.filter.matches(event)) subscription.events.push_back(event);
        }
      }
    }
    for (const access::CommandFailure& failure : boundary->drainFailures()) {
      auto range = tickets.upper_bound(failure.ticket.value);
      if (range == tickets.begin()) continue;  // J4: no connection submitted it
      --range;
      if (failure.ticket.value > range->second.last) continue;
      const auto owner = connections.find(range->second.connection);
      if (owner != connections.end()) owner->second.failures.push_back(failure);
    }
  }

  void record(std::uint64_t first, std::uint64_t last, std::uint64_t connection) {
    tickets.emplace(first, TicketRange{last, connection});
  }
};

namespace {

class InProcessConnection final : public RuntimeConnection {
 public:
  InProcessConnection(InProcessEndpoint::State& state, std::uint64_t key) : state_(&state), key_(key) {}
  ~InProcessConnection() override {
    state_->connections.erase(key_);
    // Its submissions' failures, if any are still to come, now have no owner (J4).
    for (auto range = state_->tickets.begin(); range != state_->tickets.end();) {
      range = range->second.connection == key_ ? state_->tickets.erase(range) : std::next(range);
    }
  }
  InProcessConnection(const InProcessConnection&) = delete;
  InProcessConnection& operator=(const InProcessConnection&) = delete;

  bool findEntity(const EntityGuid& entity) const override { return state_->boundary->findEntity(entity); }
  std::vector<EntityGuid> listEntities() const override { return state_->boundary->listEntities(); }
  atlantis::Result<std::vector<schema::TypeId>, access::AccessError> listComponents(
      const EntityGuid& entity) const override {
    return state_->boundary->listComponents(entity);
  }
  atlantis::Result<access::PropertyValue, access::AccessError> getProperty(
      const access::PropertyAddress& address) const override {
    return state_->boundary->getProperty(address);
  }
  std::span<const schema::TypeDescriptor> schema() const override { return atlantis::world::worldSchema(); }

  access::CommandTicket submit(access::Command command) override {
    const access::CommandTicket ticket = state_->boundary->submit(std::move(command));
    state_->record(ticket.value, ticket.value, key_);
    return ticket;
  }
  access::TransactionTicket submitTransaction(std::vector<access::Command> commands) override {
    const access::TransactionTicket ticket = state_->boundary->submitTransaction(std::move(commands));
    if (ticket.count != 0) state_->record(ticket.first.value, ticket.first.value + ticket.count - 1, key_);
    return ticket;
  }

  SubscriptionId subscribe(EventFilter filter) override {
    state_->pump();  // events already applied go to earlier subscriptions only
    const SubscriptionId id{state_->nextSubscription++};
    self().subscriptions.emplace(id.value, Subscription{std::move(filter), {}});
    return id;
  }
  atlantis::Result<std::monostate, ConnectionError> unsubscribe(SubscriptionId subscription) override {
    using ResultT = atlantis::Result<std::monostate, ConnectionError>;
    state_->pump();
    if (self().subscriptions.erase(subscription.value) == 0) return ResultT::Err(ConnectionError::UnknownSubscription);
    return ResultT::Ok(std::monostate{});
  }
  atlantis::Result<std::vector<access::Event>, ConnectionError> drainEvents(SubscriptionId subscription) override {
    using ResultT = atlantis::Result<std::vector<access::Event>, ConnectionError>;
    state_->pump();
    const auto found = self().subscriptions.find(subscription.value);
    if (found == self().subscriptions.end()) return ResultT::Err(ConnectionError::UnknownSubscription);
    return ResultT::Ok(std::exchange(found->second.events, {}));
  }
  std::vector<access::CommandFailure> drainFailures() override {
    state_->pump();
    return std::exchange(self().failures, {});
  }

 private:
  [[nodiscard]] ConnectionState& self() const { return state_->connections.at(key_); }

  InProcessEndpoint::State* state_;
  std::uint64_t key_;
};

}  // namespace

InProcessEndpoint::InProcessEndpoint(access::RuntimeWorldAccess& boundary) : state_(std::make_unique<State>()) {
  state_->boundary = &boundary;
}

InProcessEndpoint::~InProcessEndpoint() {
  ATLANTIS_CHECK_MSG(state_->connections.empty(), "InProcessEndpoint destroyed while a connection is still open");
}

std::unique_ptr<RuntimeConnection> InProcessEndpoint::open() {
  const std::uint64_t key = state_->nextConnection++;
  state_->connections.emplace(key, ConnectionState{});
  return std::make_unique<InProcessConnection>(*state_, key);
}

}  // namespace atlantis::connection

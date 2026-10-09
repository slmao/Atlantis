#pragma once

#include <atlantis/gameplay/error.h>

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/result.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <vector>

// Spec 0057 R2, Plan 0057 P2: one of the connection's pull subscriptions
// (Spec 0054 ruling Q5), owned: unsubscribed when destroyed. Move-only.
// Borrows the connection, which must outlive it. Not thread-safe.
namespace atlantis::gameplay {

class Subscription {
 public:
  Subscription(::atlantis::connection::RuntimeConnection& connection, ::atlantis::connection::SubscriptionId id)
      : connection_(&connection), id_(id) {}
  ~Subscription();
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;
  Subscription(Subscription&& other) noexcept;
  Subscription& operator=(Subscription&& other) noexcept;

  [[nodiscard]] ::atlantis::connection::SubscriptionId id() const noexcept { return id_; }

  // The matching events since the last drain, by value, in application order.
  [[nodiscard]] atlantis::Result<std::vector<::atlantis::world::access::Event>, Error> drain();

 private:
  void release() noexcept;

  ::atlantis::connection::RuntimeConnection* connection_ = nullptr;  // null once moved from
  ::atlantis::connection::SubscriptionId id_;
};

}  // namespace atlantis::gameplay

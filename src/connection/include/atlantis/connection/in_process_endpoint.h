#pragma once

#include <atlantis/connection/runtime_connection.h>

#include <memory>

namespace atlantis::world::access {
class RuntimeWorldAccess;
}

namespace atlantis::connection {

// Spec 0054 R3 / ADR-0105 D2 (ruling Q7): the InProcess implementation of
// RuntimeConnection, over the owner's RuntimeWorldAccess. The owner
// (Runtime) holds the endpoint and opens connections from it.
//
// The endpoint is the boundary's only drainer (ruling Q5, S-b; Plan 0054 J3):
// whenever a connection drains, subscribes or unsubscribes, it moves the
// boundary's events and failures out once and distributes them -- each
// event to every subscription whose filter matches, each failure to the
// connection whose recorded ticket range holds it. A failure no connection
// submitted is dropped (J4). No per-frame work, no thread.
//
// Borrowing: the endpoint borrows the boundary and must not outlive it;
// connections borrow the endpoint and must not outlive it -- its destructor
// CHECKs that every connection has been closed. Not copyable or movable,
// since connections hold its address.
class InProcessEndpoint {
 public:
  explicit InProcessEndpoint(atlantis::world::access::RuntimeWorldAccess& boundary);
  ~InProcessEndpoint();
  InProcessEndpoint(const InProcessEndpoint&) = delete;
  InProcessEndpoint& operator=(const InProcessEndpoint&) = delete;
  InProcessEndpoint(InProcessEndpoint&&) = delete;
  InProcessEndpoint& operator=(InProcessEndpoint&&) = delete;

  // A new connection; closed when the returned object is destroyed.
  [[nodiscard]] std::unique_ptr<RuntimeConnection> open();

  struct State;  // private to in_process_endpoint.cpp

 private:
  std::unique_ptr<State> state_;
};

}  // namespace atlantis::connection

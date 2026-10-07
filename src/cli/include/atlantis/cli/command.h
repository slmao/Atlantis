#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>

// Spec 0054 R7 / ADR-0105 D3 (ruling Q6, B-a): the minimal CLI -- a client
// that knows only a RuntimeConnection. Distinct from atlantis::runtime::cli,
// Runtime's startup-flag parser.
namespace atlantis::cli {

// How one command line ended (Plan 0054 P6).
enum class Outcome {
  Done,       // a query printed its answer
  Submitted,  // `entity set` submitted; its outcome prints after the next frame
  Refused,    // the boundary refused it (printed `refused <AccessError>`)
  Error,      // usage or text error (printed `error: ...`); nothing submitted
};

// Runs the six commands against `connection`, printing to `out`:
//   schema list | schema inspect <Type> | world entities | entity inspect <guid>
//   entity get <guid> <Type>.<field> | entity set <guid> <Type>.<field> <value...>
// Output is line-oriented and deterministic (schema, GUID and TypeId order).
// Frame-thread only, like the connection.
class Commands {
 public:
  Commands(atlantis::connection::RuntimeConnection& connection, std::ostream& out);
  ~Commands();
  Commands(const Commands&) = delete;
  Commands& operator=(const Commands&) = delete;

  // One command line (tokens separated by whitespace).
  Outcome run(std::string_view line);

  // Whether an `entity set` awaits its outcome.
  [[nodiscard]] bool hasPending() const noexcept { return pending_.has_value(); }
  // After the frame that applied the pending `entity set`: prints
  // `ok <guid> <Type>.<field> = <value>` or `refused <AccessError>`.
  Outcome reportPending();

 private:
  struct Pending {
    atlantis::world::access::CommandTicket ticket;
    atlantis::connection::SubscriptionId subscription;
    atlantis::world::access::PropertyAddress address;
    std::string path;
    const schema::FieldDescriptor* leaf = nullptr;
  };

  atlantis::connection::RuntimeConnection& connection_;
  std::ostream& out_;
  std::optional<Pending> pending_;
};

}  // namespace atlantis::cli

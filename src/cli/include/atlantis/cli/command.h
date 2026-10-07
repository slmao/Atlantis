#pragma once

#include <atlantis/cli/command_layer.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>

#include <ostream>
#include <string_view>

// Spec 0054 R7 / ADR-0105 D3 (ruling Q6, B-a): the CLI as a client that knows
// only a RuntimeConnection -- and, since Spec 0055 (ruling Q6, G-a), the whole
// command tree through the command layer (command_layer.h), with Spec 0054's
// six command names kept as aliases and their output unchanged. Distinct from
// atlantis::runtime::cli, Runtime's startup-flag parser.
namespace atlantis::cli {

// Runs commands against `connection`, printing each one's human text to
// `out` -- the per-frame script runner's driver (Spec 0054 ruling Q3):
//   schema list | schema inspect <Type> | world entities | entity inspect <guid>
//   entity get <guid> <Type>.<field> | entity set <guid> <Type>.<field> <value...>
// and the Spec 0055 tree (command_layer.h). Output is line-oriented and
// deterministic (schema, GUID and TypeId order); errors print `error: ...`,
// refusals `refused <AccessError>`. Frame-thread only, like the connection.
class Commands {
 public:
  // `control` (optional) serves the `runtime` commands.
  Commands(atlantis::connection::RuntimeConnection& connection, std::ostream& out,
           atlantis::connection::RuntimeControl* control = nullptr);
  ~Commands();
  Commands(const Commands&) = delete;
  Commands& operator=(const Commands&) = delete;

  // One command line (tokens separated by whitespace).
  Outcome run(std::string_view line);

  // Whether a write (`entity set`, ...) or a step awaits its outcome.
  [[nodiscard]] bool hasPending() const noexcept { return layer_.hasPending(); }
  // After the frame that applied the pending write: prints
  // `ok <guid> <Type>.<field> = <value>` (and the like for other writes) or
  // `refused <AccessError>`; Submitted while not applied yet.
  Outcome reportPending();

 private:
  CommandLayer layer_;
  std::ostream& out_;
};

}  // namespace atlantis::cli

#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/json.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Spec 0055 R4-R7 (rulings Q3-Q6; Plan 0055 P9): the CLI's command layer --
// the maintainer's command tree over a RuntimeConnection (and, for `runtime`,
// a RuntimeControl), producing for each command both its human text and its
// JSON result. Spec 0054's names stay as aliases with byte-identical human
// output (ruling Q6, G-a): `world entities` = `entity list`, `entity get` =
// `property get`, `entity set` = `property set`. Two drivers run it: the
// per-frame script runner (Commands, `--exec`) and the one-shot invocation
// and REPL of the `atlantis` executable (invocation.h).
namespace atlantis::cli {

// How one command line ended (Plan 0054 P6).
enum class Outcome {
  Done,       // a query answered, or a write's outcome arrived
  Submitted,  // a write was submitted; its outcome comes after the frame that applies it
  Refused,    // the boundary refused it
  Error,      // usage, text, connection or runtime error; nothing (further) submitted
};

// Spec 0055 ruling Q3: an error's category, which fixes its exit code.
enum class ErrorCategory { Text, Usage, Refused, Connection, Runtime };

[[nodiscard]] std::string_view toString(ErrorCategory category) noexcept;  // "text", "usage", ...
// 0 ok, 1 refused, 2 usage or text, 3 connection, 4 runtime.
[[nodiscard]] int exitCode(ErrorCategory category) noexcept;

struct CliError {
  std::string code;  // a TextError / AccessError / ConnectionError / ControlError name, or a CLI code
  ErrorCategory category = ErrorCategory::Usage;
  std::string message;                         // one sentence
  atlantis::connection::json::Value subject;   // an object: the facts to act on (guid, path, ticket, typeId, ...)
  std::string text;                            // the human line, as Spec 0054 prints it
};

// One command's answer.
struct Reply {
  std::string command;  // its tree path ("property set"; an alias reports its canonical path)
  Outcome outcome = Outcome::Done;
  std::string text;                                // human output, each line '\n'-terminated
  atlantis::connection::json::Value result;        // the JSON result (Spec 0055 ruling Q3's fields)
  std::optional<CliError> error;
};

// Many queries at once (Plan 0055 P9: filters are pipelined). The default
// asks one at a time; a transport that can pipeline supplies its own.
class QueryBatch {
 public:
  virtual ~QueryBatch() = default;
  [[nodiscard]] virtual std::vector<atlantis::Result<std::vector<schema::TypeId>, atlantis::world::access::AccessError>>
  listComponents(std::span<const atlantis::asset_system::EntityGuid> entities) = 0;
  [[nodiscard]] virtual std::vector<
      atlantis::Result<atlantis::world::access::PropertyValue, atlantis::world::access::AccessError>>
  getProperties(std::span<const atlantis::world::access::PropertyAddress> addresses) = 0;
};

// Whitespace-separated tokens.
[[nodiscard]] std::vector<std::string_view> tokenize(std::string_view line);

// Not thread-safe; the connection's thread (ADR-0004).
class CommandLayer {
 public:
  // `control` may be null (no `runtime` commands: they are a runtime error,
  // NoRuntimeControl); `batch` may be null (queries one at a time).
  explicit CommandLayer(atlantis::connection::RuntimeConnection& connection,
                        atlantis::connection::RuntimeControl* control = nullptr, QueryBatch* batch = nullptr);
  ~CommandLayer();  // abandons a pending write's subscription
  CommandLayer(const CommandLayer&) = delete;
  CommandLayer& operator=(const CommandLayer&) = delete;

  // Runs one command (its tokens; `line` is the text it came from, quoted in
  // the unknown-command error). A query answers at once. A write is
  // validated and submitted, and returns Submitted: its outcome comes from
  // poll(). One write is pending at a time; a command while one is pending
  // is an error.
  Reply run(std::span<const std::string_view> tokens, std::string_view line);

  [[nodiscard]] bool hasPending() const noexcept;
  // Looks once for the pending write's outcome: Done (its events arrived),
  // Refused (a failure names its ticket), or Submitted (not applied yet).
  Reply poll();
  // Forgets the pending write (its outcome is no longer awaited).
  void abandon();

  // Where `entity create` without a GUID gets one (RFC 9562 v4 from
  // std::random_device unless replaced -- tests make it deterministic).
  void setGuidSource(std::function<atlantis::asset_system::EntityGuid()> source);

  // Parses one `atlantis tx` line into the commands it stands for, or the
  // error that line has (Plan 0055 P9: write commands only).
  [[nodiscard]] atlantis::Result<std::vector<atlantis::world::access::Command>, CliError> parseWrite(
      std::span<const std::string_view> tokens);
  // Submits `commands` as one transaction (Spec 0053) and makes it the
  // pending write; poll() reports {committed, events} or the abort.
  Reply submitTransaction(std::vector<atlantis::world::access::Command> commands);

  struct Pending;  // private to command_layer.cpp

 private:
  atlantis::connection::RuntimeConnection& connection_;
  atlantis::connection::RuntimeControl* control_;
  QueryBatch* batch_;
  std::unique_ptr<QueryBatch> sequential_;
  std::function<atlantis::asset_system::EntityGuid()> guidSource_;
  std::unique_ptr<Pending> pending_;
};

}  // namespace atlantis::cli

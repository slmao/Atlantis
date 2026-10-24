#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/connection/json.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <cstdint>
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
  // Plan 0065 P6 (Spec 0065 third Correction): a pending `scene apply` the
  // Runtime holds (it is paused) stops being the pending command, so other
  // commands -- the step that releases it -- can run; its Applied{generation}
  // is then reported by deferredApplyOutcome(), once, whenever it has arrived.
  void deferApply();
  [[nodiscard]] std::optional<Reply> deferredApplyOutcome();

  // Where `entity create` without a GUID gets one (RFC 9562 v4 from
  // std::random_device unless replaced -- tests make it deterministic).
  void setGuidSource(std::function<atlantis::asset_system::EntityGuid()> source);

  // Plan 0065 P6 (Spec 0065 Q5, S-a; ADR-0130 D3): the `scene` group's
  // AuthoringConnection, asked for at the group's first command (the host may
  // create its session then); null, or no provider, answers NoAuthoring. The
  // connection must outlive this layer.
  using AuthoringProvider = std::function<atlantis::connection::AuthoringConnection*()>;
  void setAuthoring(AuthoringProvider provider);
  // A scene write (`set`, `create`, `destroy`, `add`, `remove`, `parent`,
  // `camera <guid>|none`, with or without a leading `scene`). `end` is the
  // node count a `create` without --at appends at; each create advances it.
  [[nodiscard]] atlantis::Result<std::vector<atlantis::connection::authoring::Command>, CliError> parseSceneWrite(
      std::span<const std::string_view> tokens, std::uint64_t& end);
  // The node count `create` appends at now; 0 without a document.
  [[nodiscard]] std::uint64_t sceneNodeCount();
  // Submits `commands` as one authoring transaction; its outcome is immediate.
  Reply submitSceneTransaction(std::vector<atlantis::connection::authoring::Command> commands, std::string command);

  // Parses one `atlantis tx` line into the commands it stands for, or the
  // error that line has (Plan 0055 P9: write commands only).
  [[nodiscard]] atlantis::Result<std::vector<atlantis::world::access::Command>, CliError> parseWrite(
      std::span<const std::string_view> tokens);
  // Submits `commands` as one transaction (Spec 0053) and makes it the
  // pending write; poll() reports {committed, events} or the abort.
  Reply submitTransaction(std::vector<atlantis::world::access::Command> commands);

  // Plan 0066 P2 (Spec 0066 R6): `runtime overlay set` -- `jsonText` is a
  // Viewport overlay in its JSON form (remote_protocol.md section 6.6), set as
  // this client's overlay. `runtime overlay select|clear` go through run().
  Reply setViewportOverlay(std::string_view jsonText);

  struct Pending;  // private to command_layer.cpp

 private:
  atlantis::connection::RuntimeConnection& connection_;
  atlantis::connection::RuntimeControl* control_;
  QueryBatch* batch_;
  std::unique_ptr<QueryBatch> sequential_;
  std::function<atlantis::asset_system::EntityGuid()> guidSource_;
  std::unique_ptr<Pending> pending_;
  // Plan 0065 P6: the `scene` group.
  [[nodiscard]] atlantis::connection::AuthoringConnection* authoring();
  Reply runScene(std::span<const std::string_view> tokens);
  // Drains the `scene` subscription, keeping a deferred apply's Applied.
  [[nodiscard]] std::vector<atlantis::connection::authoring::Event> drainSceneEvents();
  AuthoringProvider authoringProvider_;
  atlantis::connection::AuthoringConnection* authoring_ = nullptr;
  std::optional<atlantis::connection::SubscriptionId> authoringEvents_;
  bool applyDeferred_ = false;
  std::optional<std::uint64_t> deferredApplied_;  // the generation, until reported
};

}  // namespace atlantis::cli

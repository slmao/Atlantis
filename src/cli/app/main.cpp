// Spec 0055 R1 (ruling Q6, X-a; Plan 0055 P10): the `atlantis` executable --
// a machine client of a running Runtime. It resolves the session (P4),
// attaches through Atlantis Remote's client half, and runs one command or a
// REPL through Atlantis CLI's command layer. It knows nothing of Runtime,
// the ECS or the GPU stack (tests/cli/cli_boundary_tests.cpp).

#include <atlantis/cli/command_layer.h>
#include <atlantis/cli/invocation.h>
#include <atlantis/connection/json.h>
#include <atlantis/remote/remote_client.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace cli = atlantis::cli;
namespace json = atlantis::connection::json;
namespace remote = atlantis::remote;

// Filters run their queries pipelined over the session (Plan 0055 P9).
class RemoteBatch final : public cli::QueryBatch {
 public:
  explicit RemoteBatch(remote::RemoteSession& session) : session_(session) {}
  std::vector<atlantis::Result<std::vector<atlantis::schema::TypeId>, atlantis::world::access::AccessError>>
  listComponents(std::span<const atlantis::asset_system::EntityGuid> entities) override {
    return session_.listComponents(entities);
  }
  std::vector<atlantis::Result<atlantis::world::access::PropertyValue, atlantis::world::access::AccessError>>
  getProperties(std::span<const atlantis::world::access::PropertyAddress> addresses) override {
    return session_.getProperties(addresses);
  }

 private:
  remote::RemoteSession& session_;
};

[[nodiscard]] std::optional<std::string> environment(const char* name) {
  char* raw = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&raw, &length, name) != 0 || raw == nullptr) return std::nullopt;
  std::unique_ptr<char, decltype(&free)> owned(raw, &free);
  return std::string(owned.get());
}

[[nodiscard]] cli::CliError connectionError(std::string code, std::string message, json::Value facts) {
  cli::CliError error;
  error.code = std::move(code);
  error.category = cli::ErrorCategory::Connection;
  error.message = std::move(message);
  error.subject = std::move(facts);
  error.text = "error: " + error.message;
  return error;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string_view> arguments(argv + 1, argv + argc);
  cli::InvocationOptions options;
  const auto parsed = cli::parseArguments(arguments);
  if (parsed.isErr()) return cli::Invocation::reportError(parsed.error(), "", options, std::cout, std::cerr);
  const cli::Arguments& args = parsed.value();
  options.json = args.json;
  options.diagnosticsJson = args.diagnosticsJson;
  if (args.help) {
    std::cout << cli::usageText();
    return 0;
  }
  if (args.command.empty()) {
    cli::CliError error;
    error.code = "Usage";
    error.category = cli::ErrorCategory::Usage;
    error.message = "no command (atlantis --help lists them)";
    error.subject = json::Value::object();
    error.text = "error: " + error.message;
    return cli::Invocation::reportError(error, "", options, std::cout, std::cerr);
  }
  std::string commandPath = args.command[0];
  if (args.command.size() > 1) commandPath += " " + args.command[1];

  // P4: --session, then ATLANTIS_SESSION, then ./.atlantis/runtime.session.json.
  const std::optional<std::string> fromEnvironment = environment("ATLANTIS_SESSION");
  const auto sessionPath =
      remote::resolveSessionPath(args.session, fromEnvironment ? fromEnvironment->c_str() : nullptr);
  json::Value sessionFacts = json::Value::object();
  sessionFacts.set("session", json::Value::string(sessionPath.string()));
  const auto session = remote::readSessionFile(sessionPath);
  if (session.isErr()) {
    return cli::Invocation::reportError(
        connectionError(std::string(remote::toString(session.error())),
                        "no attachable Runtime: " + std::string(remote::toString(session.error())) + " (" +
                            sessionPath.string() + ")",
                        sessionFacts),
        commandPath, options, std::cout, std::cerr);
  }
  sessionFacts.set("port", json::Value::number(static_cast<std::uint64_t>(session.value().port)));
  auto attached = remote::connectRemote(session.value());
  if (attached.isErr()) {
    return cli::Invocation::reportError(
        connectionError(std::string(remote::toString(attached.error())),
                        "cannot attach to the Runtime: " + std::string(remote::toString(attached.error())),
                        sessionFacts),
        commandPath, options, std::cout, std::cerr);
  }
  remote::RemoteSession& runtime = *attached.value();
  RemoteBatch batch(runtime);
  options.transportFailure = [&runtime, sessionFacts]() -> std::optional<cli::CliError> {
    const auto failure = runtime.failure();
    if (!failure) return std::nullopt;
    return connectionError(std::string(remote::toString(*failure)),
                           "the connection to the Runtime failed: " + std::string(remote::toString(*failure)),
                           sessionFacts);
  };
  cli::Invocation invocation(runtime.connection(), &runtime.control(), &batch, std::cout, std::cerr, options);
  if (args.command[0] == "repl") {
    if (args.command.size() != 1) {
      cli::CliError error;
      error.code = "Usage";
      error.category = cli::ErrorCategory::Usage;
      error.message = "usage: repl";
      error.subject = json::Value::object();
      error.text = "error: usage: repl";
      return cli::Invocation::reportError(error, "repl", options, std::cout, std::cerr);
    }
    return invocation.repl(std::cin);
  }
  return invocation.run(args.command);
}

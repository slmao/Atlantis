#include <atlantis/cli/invocation.h>

#include <atlantis/connection/json.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace atlantis::cli {

namespace connection = atlantis::connection;
namespace json = atlantis::connection::json;
using json::Value;

namespace {

constexpr std::size_t kDiagnosticsPerCommand = 64;

constexpr std::string_view kUsage =
    "usage: atlantis [--json] [--diagnostics=json] [--session <path>] <command>\n"
    "\n"
    "  schema list | schema inspect <Type>\n"
    "  world list | world inspect\n"
    "  entity list [--with <Type>]... [--where <Type>.<field>=<value>]...\n"
    "  entity inspect <guid> | entity create [<guid>] [--component <Type>]... | entity destroy <guid>\n"
    "  component add <guid> <Type> | component remove <guid> <Type>\n"
    "  property get <guid> <Type>.<field> | property set <guid> <Type>.<field> <value...>\n"
    "  runtime pause | runtime resume | runtime step [--frames <n>] [--capture [<path>]]\n"
    "  tx <file|->       write commands, one per line, submitted as one transaction\n"
    "  repl              one command per line until exit, quit or end of input\n"
    "\n"
    "The session comes from --session, else ATLANTIS_SESSION, else ./.atlantis/runtime.session.json.\n"
    "Exit codes: 0 ok, 1 refused, 2 usage or text error, 3 connection error, 4 runtime error.\n";

[[nodiscard]] Value errorJson(const CliError& error) {
  Value out = Value::object();
  out.set("code", Value::string(error.code));
  out.set("category", Value::string(std::string(toString(error.category))));
  out.set("message", Value::string(error.message));
  out.set("subject", error.subject.isNull() ? Value::object() : error.subject);
  return out;
}

[[nodiscard]] Value diagnosticJson(const connection::Diagnostic& diagnostic) {
  Value out = Value::object();
  out.set("sequence", Value::number(diagnostic.sequence));
  out.set("severity", Value::string(std::string(connection::toString(diagnostic.severity))));
  out.set("message", Value::string(diagnostic.message));
  return out;
}

void print(const Reply& reply, const std::vector<connection::Diagnostic>& diagnostics,
           const InvocationOptions& options, std::ostream& out, std::ostream& err) {
  const bool ok = !reply.error.has_value();
  if (options.json) {
    Value envelope = Value::object();
    envelope.set("atlantis", Value::string(std::string(kEnvelopeVersion)));
    envelope.set("command", Value::string(reply.command));
    envelope.set("ok", Value::boolean(ok));
    envelope.set("result", reply.result);
    if (!ok) envelope.set("error", errorJson(*reply.error));
    Value list = Value::array();
    for (const connection::Diagnostic& diagnostic : diagnostics) list.push(diagnosticJson(diagnostic));
    envelope.set("diagnostics", std::move(list));
    out << json::write(envelope) << '\n';
  } else if (ok) {
    out << reply.text;
  } else {
    err << reply.text;
  }
  if (options.diagnosticsJson) {
    if (!ok) err << json::write(errorJson(*reply.error)) << '\n';
    for (const connection::Diagnostic& diagnostic : diagnostics) {
      Value facts = Value::object();
      facts.set("sequence", Value::number(diagnostic.sequence));
      facts.set("severity", Value::string(std::string(connection::toString(diagnostic.severity))));
      err << json::write(errorJson(CliError{"RuntimeDiagnostic", ErrorCategory::Runtime, diagnostic.message,
                                            std::move(facts), {}}))
          << '\n';
    }
  } else if (!options.json) {
    for (const connection::Diagnostic& diagnostic : diagnostics) {
      err << "runtime " << connection::toString(diagnostic.severity) << ": " << diagnostic.message << '\n';
    }
  }
  out.flush();
  err.flush();
}

[[nodiscard]] Reply errorReply(std::string command, CliError error) {
  Reply reply;
  reply.command = std::move(command);
  reply.outcome = Outcome::Error;
  reply.text = error.text + "\n";
  reply.error = std::move(error);
  return reply;
}

[[nodiscard]] CliError usage(std::string message) {
  CliError error;
  error.code = "Usage";
  error.category = ErrorCategory::Usage;
  error.message = message;
  error.subject = Value::object();
  error.text = "error: " + message;
  return error;
}

}  // namespace

std::string_view usageText() noexcept { return kUsage; }

atlantis::Result<Arguments, CliError> parseArguments(std::span<const std::string_view> arguments) {
  using ResultT = atlantis::Result<Arguments, CliError>;
  Arguments parsed;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string_view argument = arguments[i];
    if (argument == "--json") {
      parsed.json = true;
    } else if (argument == "--diagnostics=json") {
      parsed.diagnosticsJson = true;
    } else if (argument == "--help") {
      parsed.help = true;
    } else if (argument == "--session") {
      if (i + 1 >= arguments.size()) return ResultT::Err(usage("--session requires a path"));
      parsed.session = std::string(arguments[++i]);
    } else if (argument.starts_with("--diagnostics")) {
      return ResultT::Err(usage("--diagnostics takes the form --diagnostics=json"));
    } else {
      parsed.command.emplace_back(argument);
    }
  }
  return ResultT::Ok(std::move(parsed));
}

Invocation::Invocation(connection::RuntimeConnection& connection, connection::RuntimeControl* control,
                       QueryBatch* batch, std::ostream& out, std::ostream& err, InvocationOptions options)
    : layer_(connection, control, batch), control_(control), out_(out), err_(err), options_(std::move(options)) {
  if (control_ != nullptr) {
    diagnosticCursor_ = control_->diagnostics(std::numeric_limits<std::uint64_t>::max(), 0).latest;
  }
}

int Invocation::reportError(const CliError& error, std::string_view command, const InvocationOptions& options,
                            std::ostream& out, std::ostream& err) {
  print(errorReply(std::string(command), error), {}, options, out, err);
  return exitCode(error.category);
}

int Invocation::run(std::span<const std::string> command) {
  std::vector<std::string_view> tokens(command.begin(), command.end());
  std::string line;
  for (const std::string& token : command) line += (line.empty() ? "" : " ") + token;
  return runTokens(tokens, line);
}

int Invocation::runTokens(std::span<const std::string_view> tokens, std::string_view line) {
  if (!tokens.empty() && tokens[0] == "tx") return runTransaction(tokens);
  if (!tokens.empty() && tokens[0] == "repl") {
    return finish(errorReply("repl", usage("repl is a session of its own, not a command within one")));
  }
  return finish(awaitOutcome(layer_.run(tokens, line)));
}

int Invocation::runTransaction(std::span<const std::string_view> tokens) {
  if (tokens.size() != 2) return finish(errorReply("tx", usage("usage: tx <file|->")));
  std::string source;
  if (tokens[1] == "-") {
    source.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
  } else {
    std::ifstream in{std::string(tokens[1]), std::ios::binary};
    if (!in) {
      CliError error = usage("cannot read " + std::string(tokens[1]));
      error.code = "Unreadable";
      error.subject.set("file", Value::string(std::string(tokens[1])));
      return finish(errorReply("tx", std::move(error)));
    }
    source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  // Plan 0055 P9: every line is parsed before anything is submitted.
  std::vector<atlantis::world::access::Command> commands;
  std::istringstream lines(source);
  std::string text;
  std::uint64_t number = 0;
  while (std::getline(lines, text)) {
    ++number;
    const auto lineTokens = tokenize(text);
    if (lineTokens.empty() || lineTokens.front().starts_with("#")) continue;
    auto parsed = layer_.parseWrite(lineTokens);
    if (parsed.isErr()) {
      CliError error = parsed.error();
      error.subject.set("line", Value::number(number));
      error.text += " (line " + std::to_string(number) + ")";
      return finish(errorReply("tx", std::move(error)));
    }
    for (auto& command : parsed.value()) commands.push_back(std::move(command));
  }
  return finish(awaitOutcome(layer_.submitTransaction(std::move(commands))));
}

Reply Invocation::awaitOutcome(Reply reply) {
  if (reply.outcome != Outcome::Submitted) return reply;
  const bool isStep = reply.command == "runtime step";
  // A write submitted while the Runtime is paused applies only at a step:
  // its outcome is not awaited (it stays pending in the Runtime).
  if (!isStep && control_ != nullptr && control_->status().paused) {
    layer_.abandon();
    Reply pending = std::move(reply);
    pending.outcome = Outcome::Done;
    const Value* ticket = pending.result.find("ticket");
    pending.text = "pending " + (ticket != nullptr ? json::write(*ticket) : std::string("?")) +
                   " (the Runtime is paused; it applies at the next step)\n";
    Value outcome = Value::object();
    outcome.set("pending", Value::boolean(true));
    pending.result.set("outcome", std::move(outcome));
    return pending;
  }
  const auto deadline = std::chrono::steady_clock::now() + options_.outcomeTimeout;
  while (reply.outcome == Outcome::Submitted) {
    if (options_.transportFailure && options_.transportFailure()) break;
    if (std::chrono::steady_clock::now() >= deadline) {
      const std::string command = reply.command;
      layer_.abandon();
      CliError error;
      error.code = "OutcomeTimeout";
      error.category = ErrorCategory::Runtime;
      error.message = "no outcome within " + std::to_string(options_.outcomeTimeout.count()) + " ms";
      error.subject = reply.result.isNull() ? Value::object() : reply.result;
      error.text = "error: " + error.message;
      return errorReply(command, std::move(error));
    }
    if (options_.awaitFrame) options_.awaitFrame();
    reply = layer_.poll();
  }
  return reply;
}

int Invocation::finish(Reply reply) {
  if (options_.transportFailure) {
    if (std::optional<CliError> failure = options_.transportFailure()) {
      layer_.abandon();
      return reportError(*failure, reply.command, options_, out_, err_);
    }
  }
  std::vector<connection::Diagnostic> diagnostics;
  if (control_ != nullptr) {
    connection::DiagnosticBatch batch = control_->diagnostics(diagnosticCursor_, kDiagnosticsPerCommand);
    diagnostics = std::move(batch.entries);
    diagnosticCursor_ = std::max(diagnosticCursor_, batch.latest);
  }
  print(reply, diagnostics, options_, out_, err_);
  return reply.error ? exitCode(reply.error->category) : 0;
}

int Invocation::repl(std::istream& in) {
  std::string line;
  while (true) {
    err_ << "atlantis> " << std::flush;
    if (!std::getline(in, line)) break;
    const std::vector<std::string_view> tokens = tokenize(line);
    if (tokens.empty()) continue;
    if (tokens.size() == 1 && (tokens[0] == "exit" || tokens[0] == "quit")) break;
    (void)runTokens(tokens, line);
  }
  err_ << '\n' << std::flush;
  return 0;
}

}  // namespace atlantis::cli

#pragma once

#include <atlantis/cli/command_layer.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/result.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Spec 0055 R1, R4-R7 (rulings Q3-Q5; Plan 0055 P9): the `atlantis`
// executable's driver over the command layer -- one command per invocation,
// or a REPL -- with its output contract:
//   human (default)   the command's text on stdout; errors on stderr
//   --json            one envelope per command on stdout:
//                       {"atlantis":"cli/1","command":"<tree path>","ok":<bool>,
//                        "result":<command fields>,["error":{code,category,message,subject},]
//                        "diagnostics":[{sequence,severity,message}...]}
//   --diagnostics=json  every error and Runtime diagnostic also on stderr, one
//                       JSON object per line ({code,category,message,subject})
// Exit codes: 0 ok, 1 refused, 2 usage or text, 3 connection, 4 runtime.
// Writes wait for their outcome (Spec 0055 ruling Q4, T-a), polling once per
// frame boundary, up to a time limit (a runtime error, WriteTimeout).
namespace atlantis::cli {

inline constexpr std::string_view kEnvelopeVersion = "cli/1";

// The usage text `atlantis --help` prints.
[[nodiscard]] std::string_view usageText() noexcept;

// The command line, with the global flags taken out wherever they appear.
struct Arguments {
  bool json = false;
  bool diagnosticsJson = false;
  bool help = false;
  std::optional<std::string> session;  // --session <path>
  std::vector<std::string> command;    // everything else, in order
};
[[nodiscard]] atlantis::Result<Arguments, CliError> parseArguments(std::span<const std::string_view> arguments);

struct InvocationOptions {
  bool json = false;
  bool diagnosticsJson = false;
  // How long a write (or a step) may wait for its outcome (Plan 0055 P9).
  std::chrono::milliseconds outcomeTimeout{10000};
  // Called between two polls of a pending outcome. Over a transport each
  // poll already waits for the next frame boundary, so production passes
  // none; an in-process test runs a frame here.
  std::function<void()> awaitFrame;
  // A transport's failure, checked after each command: a connection error
  // replaces whatever the command printed (its answers were empty values).
  std::function<std::optional<CliError>()> transportFailure;
};

// Not thread-safe; the connection's thread.
class Invocation {
 public:
  // `control` and `batch` as for CommandLayer. With a control, the Runtime's
  // diagnostics newer than this construction ride in each envelope.
  Invocation(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl* control,
             QueryBatch* batch, std::ostream& out, std::ostream& err, InvocationOptions options);

  // Runs one command (`tx <file|->` included) and returns its exit code.
  int run(std::span<const std::string> command);
  // `atlantis repl`: one command per line from `in` (the prompt `atlantis> `
  // goes to stderr, so stdout carries only output) until `exit`, `quit` or
  // end of input; a command's error does not end the session. Returns 0.
  int repl(std::istream& in);

  // Prints `error` as the outcome of `command` (a failure before any
  // connection exists: no session file, nothing listening). Returns its exit
  // code.
  static int reportError(const CliError& error, std::string_view command, const InvocationOptions& options,
                         std::ostream& out, std::ostream& err);

 private:
  int runTokens(std::span<const std::string_view> tokens, std::string_view line);
  int runTransaction(std::span<const std::string_view> tokens);
  int finish(Reply reply);
  Reply awaitOutcome(Reply reply);

  CommandLayer layer_;
  atlantis::connection::RuntimeControl* control_;
  std::ostream& out_;
  std::ostream& err_;
  InvocationOptions options_;
  std::uint64_t diagnosticCursor_ = 0;
};

}  // namespace atlantis::cli

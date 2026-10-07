#pragma once

#include <atlantis/cli/command.h>
#include <atlantis/connection/runtime_connection.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::cli {

// The script's lines (split on '\n'; a trailing '\r' is removed).
[[nodiscard]] std::vector<std::string> splitLines(std::string_view script);

// Spec 0054 R9 / ADR-0105 D7 (ruling Q3, H-a; Plan 0054 P7, J9): runs a
// script one command per frame. The host calls step() once after each
// runFrame(), on the frame thread:
//   1. the previous `entity set`'s outcome is reported (it applied at the
//      start of the frame just run), and
//   2. the next command is echoed as `> <line>` and run -- a query reads the
//      world that frame rendered; a set submits for the next frame.
// Blank lines and lines whose first non-space character is '#' are skipped
// without taking a frame; a line with an error prints `error: ...` and the
// script goes on. After the last outcome it prints, once:
//   # exec done: <n> commands, <e> errors, <r> refused
// Nothing here blocks.
class ScriptRunner {
 public:
  ScriptRunner(atlantis::connection::RuntimeConnection& connection, std::vector<std::string> lines, std::ostream& out);

  void step();
  [[nodiscard]] bool finished() const noexcept { return finished_; }

 private:
  Commands commands_;
  std::vector<std::string> lines_;
  std::ostream& out_;
  std::size_t next_ = 0;
  std::size_t commandCount_ = 0;
  std::size_t errorCount_ = 0;
  std::size_t refusedCount_ = 0;
  bool finished_ = false;
};

}  // namespace atlantis::cli

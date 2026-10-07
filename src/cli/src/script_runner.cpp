#include <atlantis/cli/script_runner.h>

#include <utility>

namespace atlantis::cli {

namespace {

[[nodiscard]] bool isSkipped(std::string_view line) {
  const auto first = line.find_first_not_of(" \t\r");
  return first == std::string_view::npos || line[first] == '#';
}

}  // namespace

std::vector<std::string> splitLines(std::string_view script) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= script.size()) {
    const auto end = script.find('\n', start);
    std::string_view line = script.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    lines.emplace_back(line);
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return lines;
}

ScriptRunner::ScriptRunner(atlantis::connection::RuntimeConnection& connection, std::vector<std::string> lines,
                           std::ostream& out)
    : commands_(connection, out), lines_(std::move(lines)), out_(out) {}

void ScriptRunner::step() {
  if (finished_) return;
  if (commands_.hasPending()) {
    const Outcome outcome = commands_.reportPending();
    if (outcome == Outcome::Submitted) return;  // not applied yet
    if (outcome == Outcome::Refused) ++refusedCount_;
  }
  while (next_ < lines_.size() && isSkipped(lines_[next_])) ++next_;
  if (next_ < lines_.size()) {
    const std::string& line = lines_[next_++];
    out_ << "> " << line << '\n';
    ++commandCount_;
    const Outcome outcome = commands_.run(line);
    if (outcome == Outcome::Error) ++errorCount_;
    if (outcome == Outcome::Refused) ++refusedCount_;
    return;
  }
  if (!commands_.hasPending()) {
    out_ << "# exec done: " << commandCount_ << " commands, " << errorCount_ << " errors, " << refusedCount_
         << " refused\n";
    out_.flush();
    finished_ = true;
  }
}

}  // namespace atlantis::cli

#include <atlantis/cli/command.h>

#include <vector>

namespace atlantis::cli {

Commands::Commands(atlantis::connection::RuntimeConnection& connection, std::ostream& out,
                   atlantis::connection::RuntimeControl* control)
    : layer_(connection, control), out_(out) {}

Commands::~Commands() = default;

Outcome Commands::run(std::string_view line) {
  const std::vector<std::string_view> tokens = tokenize(line);
  const Reply reply = layer_.run(tokens, line);
  out_ << reply.text;
  return reply.outcome;
}

Outcome Commands::reportPending() {
  if (!layer_.hasPending()) return Outcome::Done;
  const Reply reply = layer_.poll();
  out_ << reply.text;
  return reply.outcome;
}

}  // namespace atlantis::cli

#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/result.h>

#include <cstdlib>
#include <string>

namespace {

atlantis::Result<int, std::string> parsePositive(int value) {
  if (value > 0) {
    return atlantis::Result<int, std::string>::Ok(value);
  }
  return atlantis::Result<int, std::string>::Err("value must be positive");
}

}  // namespace

int main() {
  ATLANTIS_LOG_INFO("Atlantis foundation demo starting");
  ATLANTIS_LOG_DEBUG("This is a debug-level message");
  ATLANTIS_LOG_WARN("This is a warn-level message");

  auto ok = parsePositive(42);
  ATLANTIS_CHECK(ok.isOk());
  ATLANTIS_LOG_INFO("parsePositive(42) = {}", ok.value());

  auto failed = parsePositive(-1);
  if (failed.isErr()) {
    ATLANTIS_LOG_INFO("parsePositive(-1) failed as expected: {}", failed.error());
  }

  ATLANTIS_ASSERT(1 + 1 == 2);

  ATLANTIS_LOG_INFO("Atlantis foundation demo finished");
  return EXIT_SUCCESS;
}

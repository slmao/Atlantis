#include <atlantis/runtime/exit_reason.h>

#include <atlantis/assert.h>

#include <cstdlib>

namespace atlantis::runtime {

int toProcessExitCode(RuntimeExitReason reason) noexcept {
  switch (reason) {  // no default -- see Plan 0013 Section D3 (2026-08-21 amendment)
    case RuntimeExitReason::Success:
      return EXIT_SUCCESS;
    case RuntimeExitReason::InitializationFailed:
      return 1;
    case RuntimeExitReason::UnrecoverableRuntimeError:
      return 2;
  }
  // Reached only if a future RuntimeExitReason value is added without a
  // corresponding case above. ATLANTIS_CHECK_MSG's own installed handler
  // is not guaranteed to terminate, so a fallback return is required, not
  // optional -- see Plan 0013 Section D3. Do NOT turn the switch above
  // into a `default:` case instead: that would additionally suppress the
  // C4062 diagnostic this target's /w14062 compile option relies on to
  // catch a missing case at build time.
  ATLANTIS_CHECK_MSG(false, "toProcessExitCode(): unhandled RuntimeExitReason enumerator");
  return 2;
}

}  // namespace atlantis::runtime

#pragma once

namespace atlantis::runtime {

// See Plan 0013 Section D3. RuntimeApplication::shutdown() only ever
// produces Success or UnrecoverableRuntimeError -- InitializationFailed
// is produced directly by createRuntimeApplication()'s own Err path
// (main.cpp maps it without ever constructing a RuntimeApplication),
// never via shutdown()'s own return value.
enum class RuntimeExitReason {
  Success,
  InitializationFailed,
  UnrecoverableRuntimeError,
};

// Success -> EXIT_SUCCESS (0); the other two each map to their own
// distinct, fixed nonzero value. Exact integers are a source-level
// detail -- three distinct values is the requirement, not their literal
// numbers.
[[nodiscard]] int toProcessExitCode(RuntimeExitReason reason) noexcept;

}  // namespace atlantis::runtime

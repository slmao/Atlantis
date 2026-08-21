#include <atlantis/runtime/exit_reason.h>

#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::RuntimeExitReason;
using atlantis::runtime::toProcessExitCode;

TEST_CASE("Success maps to EXIT_SUCCESS", "[runtime][exit_reason]") {
  REQUIRE(toProcessExitCode(RuntimeExitReason::Success) == EXIT_SUCCESS);
}

TEST_CASE("The three RuntimeExitReason values map to three distinct process exit codes", "[runtime][exit_reason]") {
  const int success = toProcessExitCode(RuntimeExitReason::Success);
  const int initFailed = toProcessExitCode(RuntimeExitReason::InitializationFailed);
  const int unrecoverable = toProcessExitCode(RuntimeExitReason::UnrecoverableRuntimeError);

  REQUIRE(success != initFailed);
  REQUIRE(success != unrecoverable);
  REQUIRE(initFailed != unrecoverable);
}

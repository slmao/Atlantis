#include <cstdlib>
#include <filesystem>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "../../../src/tools/shader_compiler/command_line_quoting.h"
#include "../../../src/tools/shader_compiler/process_launch.h"

using atlantis::tools::shader_compiler::launchProcess;
using atlantis::tools::shader_compiler::ProcessLaunchError;

namespace {

// Resolves cmd.exe via the COMSPEC environment variable (always set on
// Windows), never a plain "cmd.exe" left for launchProcess()'s own
// std::filesystem::exists() check to reject -- COMSPEC already gives an
// absolute path.
[[nodiscard]] std::filesystem::path resolveCmdExe() {
  char* rawValue = nullptr;
  std::size_t length = 0;
  const errno_t error = _dupenv_s(&rawValue, &length, "COMSPEC");
  std::unique_ptr<char, decltype(&free)> owned(rawValue, &free);
  REQUIRE(error == 0);
  REQUIRE(owned != nullptr);
  return std::filesystem::path(owned.get());
}

}  // namespace

TEST_CASE("quoteArgument() follows the standard Windows/MSVC argument-quoting convention",
          "[tools][shader_compiler][process_launch]") {
  using atlantis::tools::shader_compiler::detail::quoteArgument;

  SECTION("no special characters needs no quoting") {
    REQUIRE(quoteArgument(L"plain") == L"plain");
  }

  SECTION("an embedded space forces quoting") {
    REQUIRE(quoteArgument(L"has space") == L"\"has space\"");
  }

  SECTION("an embedded double quote is escaped") {
    REQUIRE(quoteArgument(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
  }

  SECTION("a trailing backslash immediately before the closing quote is doubled") {
    // Needs quoting (contains a space) AND ends with a backslash that
    // directly abuts the synthetic closing quote this function adds --
    // that backslash must be doubled, or the closing quote it precedes
    // would be misread as an escaped literal quote instead of the
    // argument's own terminator.
    REQUIRE(quoteArgument(L"C:\\has space\\") == L"\"C:\\has space\\\\\"");
  }

  SECTION("a backslash immediately before an embedded literal quote is doubled") {
    REQUIRE(quoteArgument(L"a\\\"b") == L"\"a\\\\\\\"b\"");
  }

  SECTION("an empty string argument is quoted as \"\"") {
    REQUIRE(quoteArgument(L"") == L"\"\"");
  }

  SECTION("a path under a directory whose own name contains a space") {
    REQUIRE(quoteArgument(L"C:\\Program Files\\Atlantis\\slangc.exe") ==
            L"\"C:\\Program Files\\Atlantis\\slangc.exe\"");
  }

  SECTION("backslashes not followed by a quote are left untouched") {
    REQUIRE(quoteArgument(L"C:\\no\\spaces\\here") == L"C:\\no\\spaces\\here");
  }
}

TEST_CASE("launchProcess() a genuinely nonexistent executable path is ExecutableNotFound",
          "[tools][shader_compiler][process_launch]") {
  const auto result = launchProcess("Z:\\this\\path\\does\\not\\exist.exe", {});
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ProcessLaunchError::ExecutableNotFound);
}

TEST_CASE("launchProcess() end-to-end happy path via cmd.exe", "[tools][shader_compiler][process_launch]") {
  const auto cmdExe = resolveCmdExe();

  SECTION("a zero exit code is captured correctly") {
    const auto result = launchProcess(cmdExe, {cmdExe.string(), "/c", "exit 0"});
    REQUIRE(result.isOk());
    REQUIRE(result.value().exitCode == 0);
  }

  SECTION("a nonzero exit code is captured correctly") {
    const auto result = launchProcess(cmdExe, {cmdExe.string(), "/c", "exit 3"});
    REQUIRE(result.isOk());
    REQUIRE(result.value().exitCode == 3);
  }

  SECTION("stdout and stderr are both captured into the single combined diagnostics field") {
    const auto result =
        launchProcess(cmdExe, {cmdExe.string(), "/c", "echo out-marker && echo err-marker 1>&2"});
    REQUIRE(result.isOk());
    REQUIRE(result.value().diagnostics.find("out-marker") != std::string::npos);
    REQUIRE(result.value().diagnostics.find("err-marker") != std::string::npos);
  }
}

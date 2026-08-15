#pragma once

// Private to atlantis_shader_compiler -- the pure, process-free half of
// process_launch.cpp's own command-line construction (Plan 0008 Section
// 4), split out so it is directly unit-testable in isolation (Plan 0008
// Section 9) without spawning a real process. Not part of
// process_launch.h's public surface.

#include <string>
#include <vector>

namespace atlantis::tools::shader_compiler::detail {

[[nodiscard]] std::wstring utf8ToWide(const std::string& utf8);

// Standard Windows/MSVC C runtime argument-quoting convention (the same
// one CommandLineToArgvW() consumes): an argument containing whitespace,
// a double quote, or that is empty is wrapped in "; embedded "
// characters are escaped as \"; a run of N backslashes immediately
// preceding a " (literal or the argument's own closing quote) is
// doubled to 2N backslashes; a run of backslashes not immediately
// followed by a " is left untouched.
[[nodiscard]] std::wstring quoteArgument(const std::wstring& arg);

// Joins `arguments` (UTF-8) into a single, space-separated, individually
// quoted UTF-16 command-line string suitable for CreateProcessW's own
// lpCommandLine parameter (the caller is responsible for giving
// CreateProcessW a mutable buffer -- this function only builds the
// content).
[[nodiscard]] std::wstring buildCommandLine(const std::vector<std::string>& arguments);

}  // namespace atlantis::tools::shader_compiler::detail
